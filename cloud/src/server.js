"use strict";

// NOTE: This is an in-memory development simulator for the Cheeko cloud API.
// All state lives in this Node process and is lost on restart, and the
// "dev-signature" values are placeholders, not cryptographic signatures.
// It exists for local demos and contract exploration, not production use.

const http = require("node:http");
const { createHash, randomUUID } = require("node:crypto");
const {
  DeploymentTransport,
  JobStatus,
  PairingMethod,
  Permission,
  json,
  routeNotFound,
} = require("./contracts");

const PORT = Number(process.env.PORT || 8787);

const state = {
  pairingSessions: new Map(),
  devices: new Map(),
  generationJobs: new Map(),
  apps: new Map(),
  artifacts: new Map(),
  deployments: new Map(),
};

function nowIso() {
  return new Date().toISOString();
}

function minutesFromNow(minutes) {
  return new Date(Date.now() + minutes * 60 * 1000).toISOString();
}

function sha256(value) {
  return createHash("sha256").update(value).digest("hex");
}

function buildGeneratedPackage(body, appId) {
  const appName = body.name || "Generated Cheeko App";
  const source = `#include "cheeko.h"

using namespace cheeko;

class GeneratedCheekoApp : public CheekoApp {
 public:
  void OnStart() override {
    Cheeko().display().CenterText(42, "${appName}");
    Cheeko().display().CenterText(124, "built from cloud");
  }

  void OnTouch(const TouchEvent& event) override {
    if (event.pressed) {
      Cheeko().speaker().Tone(880, 90);
      Cheeko().cloud().SendText("Cheekoai app says hello");
    }
  }
};

CHEEKO_APP(GeneratedCheekoApp);
`;
  const packageHash = sha256(source);
  const manifest = {
    schema: "ai.cheeko.app.v1",
    app_id: appId,
    name: appName,
    version: "0.1.0",
    sdk: { min: "0.1.0", max: "0.x" },
    boards: [body.hardwareModel || "cheeko-gotchi"],
    entrypoint: "CreateCheekoApp",
    permissions: body.requestedPermissions || [
      Permission.DISPLAY,
      Permission.TOUCH,
      Permission.WIFI,
      Permission.CLOUD_FETCH,
      Permission.SPEAKER_TONE,
    ],
    artifacts: [
      {
        type: "native",
        path: "app.bin",
        sha256: packageHash,
      },
    ],
    rollback: {
      keep_previous: true,
      healthcheck_seconds: 15,
    },
  };

  return {
    manifest,
    packageHash,
    sourcePreview: source,
  };
}

async function readJson(request) {
  const chunks = [];
  for await (const chunk of request) {
    chunks.push(chunk);
  }

  if (chunks.length === 0) {
    return {};
  }

  return JSON.parse(Buffer.concat(chunks).toString("utf8"));
}

function createPairingSession(body) {
  const pairingSessionId = `pair_${randomUUID()}`;
  const session = {
    pairingSessionId,
    ownerId: body.ownerId,
    pairingMethod: body.pairingMethod || PairingMethod.QR_CODE,
    deviceClaim: body.deviceClaim || {},
    pairingCode: String(Math.floor(100000 + Math.random() * 900000)),
    expiresAt: minutesFromNow(10),
    status: "pending",
  };

  state.pairingSessions.set(pairingSessionId, session);
  return json(201, session);
}

function claimDevice(deviceId, body) {
  const session = state.pairingSessions.get(body.pairingSessionId);
  if (!session) {
    return json(403, { error: "pairing_invalid", reason: "session_not_found" });
  }
  if (new Date(session.expiresAt).getTime() < Date.now()) {
    return json(403, { error: "pairing_invalid", reason: "session_expired" });
  }
  if (session.status !== "pending") {
    return json(403, { error: "pairing_invalid", reason: "session_already_used" });
  }
  if (body.pairingCode !== session.pairingCode) {
    return json(403, { error: "pairing_invalid", reason: "pairing_code_mismatch" });
  }

  session.status = "claimed";

  const device = {
    deviceId,
    ownerId: body.ownerId,
    pairingSessionId: body.pairingSessionId,
    serial: body.serial,
    hardwareModel: body.hardwareModel || "cheeko-gotchi",
    pairedAt: nowIso(),
    status: "paired",
  };

  state.devices.set(deviceId, device);
  return json(200, device);
}

function createGenerationJob(body) {
  const jobId = `job_${randomUUID()}`;
  const appId = body.appId || `com.cheeko.generated.${randomUUID().slice(0, 8)}`;
  const artifactId = `art_${randomUUID()}`;
  const permissions = body.requestedPermissions || [
    Permission.DISPLAY,
    Permission.TOUCH,
    Permission.WIFI,
    Permission.CLOUD_FETCH,
  ];
  const generatedPackage = buildGeneratedPackage({ ...body, requestedPermissions: permissions }, appId);

  const app = {
    appId,
    ownerId: body.ownerId,
    name: body.name || "Generated Cheeko App",
    prompt: body.prompt,
    permissions,
    visibility: "private",
    createdAt: nowIso(),
  };

  const artifact = {
    artifactId,
    appId,
    version: "0.1.0",
    packageDigest: `sha256:${generatedPackage.packageHash}`,
    signature: `dev-signature:${sha256(`${artifactId}:${generatedPackage.packageHash}`).slice(0, 32)}`,
    signed: true,
    sdkTarget: body.targetSdk || "0.1",
    manifest: generatedPackage.manifest,
    sourcePreview: generatedPackage.sourcePreview,
  };

  const job = {
    jobId,
    ownerId: body.ownerId,
    deviceId: body.deviceId,
    status: JobStatus.READY,
    prompt: body.prompt,
    app,
    artifact,
    logs: [
      "queued generation job",
      "generated app skeleton",
      "built package",
      "signed artifact",
    ],
    statusUrl: `/v1/apps/generation-jobs/${jobId}`,
    createdAt: nowIso(),
  };

  state.apps.set(appId, app);
  state.artifacts.set(artifactId, artifact);
  state.generationJobs.set(jobId, job);
  return json(202, job);
}

function getGenerationJob(jobId) {
  const job = state.generationJobs.get(jobId);
  return job ? json(200, job) : json(404, { error: "job_not_found" });
}

function getArtifact(appId, artifactId) {
  const artifact = state.artifacts.get(artifactId);
  if (!artifact || artifact.appId !== appId) {
    return json(404, { error: "artifact_not_found" });
  }

  return json(200, {
    ...artifact,
    downloadUrl: `/v1/apps/${appId}/artifacts/${artifactId}/download`,
    expiresAt: minutesFromNow(15),
  });
}

function downloadArtifact(appId, artifactId) {
  const artifact = state.artifacts.get(artifactId);
  if (!artifact || artifact.appId !== appId) {
    return json(404, { error: "artifact_not_found" });
  }

  // The generated source doubles as the mock's "package bytes".
  return {
    statusCode: 200,
    headers: {
      "content-type": "text/plain; charset=utf-8",
      "cache-control": "no-store",
      "x-cheeko-digest": artifact.packageDigest,
    },
    body: artifact.sourcePreview,
  };
}

function createDeployment(deviceId, body) {
  const deploymentId = `dep_${randomUUID()}`;
  const deployment = {
    deploymentId,
    deviceId,
    appId: body.appId,
    artifactId: body.artifactId,
    transport: body.transport || DeploymentTransport.WIFI_OTA,
    installMode: body.installMode || "replace_current",
    status: "commanded",
    commandedAt: nowIso(),
    lastReport: null,
  };

  state.deployments.set(deploymentId, deployment);
  return json(202, deployment);
}

function reportDeployment(deviceId, deploymentId, body) {
  const deployment = state.deployments.get(deploymentId);
  if (!deployment || deployment.deviceId !== deviceId) {
    return json(404, { error: "deployment_not_found" });
  }

  const updated = {
    ...deployment,
    status: body.status || deployment.status,
    lastReport: {
      status: body.status || deployment.status,
      message: body.message || "",
      appId: body.appId || deployment.appId,
      reportedAt: nowIso(),
    },
  };

  if (updated.status === "installed") {
    updated.installedAt = nowIso();
  }

  state.deployments.set(deploymentId, updated);
  return json(200, updated);
}

function listOwnerApps(ownerId) {
  const apps = Array.from(state.apps.values()).filter(
    (app) => app.ownerId === ownerId,
  );
  return json(200, { apps });
}

function submitMarketplaceApp(body) {
  const app = state.apps.get(body.appId);
  if (!app) {
    return json(404, { error: "app_not_found" });
  }

  const listing = {
    ...app,
    visibility: "marketplace_review",
    submittedAt: nowIso(),
    reviewStatus: "pending_security_review",
  };

  state.apps.set(body.appId, listing);
  return json(202, listing);
}

function approveMarketplaceApp(appId) {
  const app = state.apps.get(appId);
  if (!app) {
    return json(404, { error: "app_not_found" });
  }

  const approved = {
    ...app,
    visibility: "public",
    reviewStatus: "approved",
    approvedAt: nowIso(),
  };

  state.apps.set(appId, approved);
  return json(200, approved);
}

function listMarketplaceApps() {
  const apps = Array.from(state.apps.values()).filter(
    (app) => app.visibility === "public",
  );
  return json(200, { apps });
}

function recordHeartbeat(deviceId, body) {
  const pendingCommands = Array.from(state.deployments.values()).filter(
    (deployment) =>
      deployment.deviceId === deviceId && deployment.status === "commanded",
  );

  state.devices.set(deviceId, {
    ...(state.devices.get(deviceId) || { deviceId }),
    lastHeartbeatAt: nowIso(),
    heartbeat: body,
  });

  return json(200, {
    accepted: true,
    serverTime: nowIso(),
    pendingCommands,
  });
}

async function handle(request) {
  const url = new URL(request.url, `http://${request.headers.host}`);
  const method = request.method || "GET";
  const path = url.pathname.replace(/^\/v1/, "");
  const body = method === "GET" ? {} : await readJson(request);

  if (method === "GET" && path === "/health") {
    return json(200, { ok: true, service: "cheeko-cloud-builder" });
  }

  if (method === "POST" && path === "/devices/pairing-sessions") {
    return createPairingSession(body);
  }

  const claimMatch = path.match(/^\/devices\/([^/]+)\/claim$/);
  if (method === "POST" && claimMatch) {
    return claimDevice(claimMatch[1], body);
  }

  if (method === "POST" && path === "/apps/generation-jobs") {
    return createGenerationJob(body);
  }

  const jobMatch = path.match(/^\/apps\/generation-jobs\/([^/]+)$/);
  if (method === "GET" && jobMatch) {
    return getGenerationJob(jobMatch[1]);
  }

  const artifactDownloadMatch = path.match(
    /^\/apps\/([^/]+)\/artifacts\/([^/]+)\/download$/,
  );
  if (method === "GET" && artifactDownloadMatch) {
    return downloadArtifact(artifactDownloadMatch[1], artifactDownloadMatch[2]);
  }

  const artifactMatch = path.match(/^\/apps\/([^/]+)\/artifacts\/([^/]+)$/);
  if (method === "GET" && artifactMatch) {
    return getArtifact(artifactMatch[1], artifactMatch[2]);
  }

  const deploymentMatch = path.match(/^\/devices\/([^/]+)\/deployments$/);
  if (method === "POST" && deploymentMatch) {
    return createDeployment(deploymentMatch[1], body);
  }

  const deploymentReportMatch = path.match(
    /^\/devices\/([^/]+)\/deployments\/([^/]+)\/report$/,
  );
  if (method === "POST" && deploymentReportMatch) {
    return reportDeployment(deploymentReportMatch[1], deploymentReportMatch[2], body);
  }

  const ownerAppsMatch = path.match(/^\/owners\/([^/]+)\/apps$/);
  if (method === "GET" && ownerAppsMatch) {
    return listOwnerApps(ownerAppsMatch[1]);
  }

  if (method === "POST" && path === "/marketplace/apps") {
    return submitMarketplaceApp(body);
  }

  const approveMatch = path.match(/^\/marketplace\/apps\/([^/]+)\/approve$/);
  if (method === "POST" && approveMatch) {
    return approveMarketplaceApp(approveMatch[1]);
  }

  if (method === "GET" && path === "/marketplace/apps") {
    return listMarketplaceApps();
  }

  const heartbeatMatch = path.match(/^\/devices\/([^/]+)\/heartbeat$/);
  if (method === "POST" && heartbeatMatch) {
    return recordHeartbeat(heartbeatMatch[1], body);
  }

  return routeNotFound(method, path);
}

const server = http.createServer(async (request, response) => {
  try {
    const result = await handle(request);
    response.writeHead(result.statusCode, result.headers);
    response.end(result.body);
  } catch (error) {
    const result = json(400, {
      error: "bad_request",
      message: error.message,
    });
    response.writeHead(result.statusCode, result.headers);
    response.end(result.body);
  }
});

if (require.main === module) {
  server.listen(PORT, () => {
    console.log(`Cheeko Cloud Builder listening on http://localhost:${PORT}`);
  });
}

module.exports = {
  handle,
  server,
  state,
};
