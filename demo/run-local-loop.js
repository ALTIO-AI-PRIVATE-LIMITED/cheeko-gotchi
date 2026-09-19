"use strict";

const fs = require("node:fs");
const http = require("node:http");
const path = require("node:path");
const { server } = require("../cloud/src/server");

const ROOT = path.resolve(__dirname, "..");
const OUT_DIR = path.join(ROOT, "demo", "out");
const PORT = Number(process.env.CHEEKOAI_DEMO_PORT || 8797);
const BASE_URL = `http://localhost:${PORT}/v1`;

function request(method, pathname, body) {
  const payload = body ? JSON.stringify(body) : "";
  return new Promise((resolve, reject) => {
    const req = http.request(
      `${BASE_URL}${pathname}`,
      {
        method,
        headers: {
          "content-type": "application/json",
          "content-length": Buffer.byteLength(payload),
        },
      },
      (res) => {
        const chunks = [];
        res.on("data", (chunk) => chunks.push(chunk));
        res.on("end", () => {
          const text = Buffer.concat(chunks).toString("utf8");
          const json = text ? JSON.parse(text) : {};
          if (res.statusCode >= 400) {
            reject(new Error(`${method} ${pathname} -> ${res.statusCode}: ${text}`));
            return;
          }
          resolve(json);
        });
      },
    );
    req.on("error", reject);
    req.end(payload);
  });
}

function verifyArtifact(artifact) {
  const digest = artifact.packageDigest || "";
  const manifestHash = artifact.manifest?.artifacts?.[0]?.sha256;
  const problems = [];

  if (!artifact.signed) problems.push("artifact is not signed");
  if (!digest.startsWith("sha256:")) problems.push("package digest is not sha256");
  if (!manifestHash) problems.push("manifest is missing app artifact hash");
  if (manifestHash && digest !== `sha256:${manifestHash}`) {
    problems.push("manifest hash does not match package digest");
  }
  if (!artifact.manifest?.permissions?.includes("display")) {
    problems.push("display permission missing");
  }

  return {
    ok: problems.length === 0,
    problems,
  };
}

async function run() {
  await new Promise((resolve) => server.listen(PORT, resolve));

  const ownerId = "owner_demo";
  const deviceId = "dev_cheekoai_demo_001";
  const serial = "CAI-LOCAL-0001";
  const log = [];

  try {
    const health = await request("GET", "/health");
    log.push({ step: "cloud health", result: health });

    const pairing = await request("POST", "/devices/pairing-sessions", {
      ownerId,
      pairingMethod: "manual_code",
      deviceClaim: {
        serial,
        hardwareModel: "cheeko-gotchi",
      },
    });
    log.push({ step: "phone creates pairing session", result: pairing });

    const claimed = await request("POST", `/devices/${deviceId}/claim`, {
      ownerId,
      pairingSessionId: pairing.pairingSessionId,
      serial,
      hardwareModel: "cheeko-gotchi",
      pairingCode: pairing.pairingCode,
    });
    log.push({ step: "device claims owner pairing", result: claimed });

    const job = await request("POST", "/apps/generation-jobs", {
      ownerId,
      deviceId,
      name: "Weather Face",
      appId: "com.cheeko.weather_face",
      prompt: "Make a desk buddy that blinks, talks, and shows weather.",
      targetSdk: "0.1.0",
      requestedPermissions: ["display", "touch", "wifi", "cloud.fetch", "speaker.tone"],
    });
    log.push({ step: "cloud builder generates app", result: job });

    const artifact = await request(
      "GET",
      `/apps/${job.app.appId}/artifacts/${job.artifact.artifactId}`,
    );
    const verification = verifyArtifact(artifact);
    log.push({ step: "device verifies generated artifact", result: verification });
    if (!verification.ok) {
      throw new Error(`artifact verification failed: ${verification.problems.join(", ")}`);
    }

    const deployment = await request("POST", `/devices/${deviceId}/deployments`, {
      appId: job.app.appId,
      artifactId: job.artifact.artifactId,
      transport: "wifi_ota",
      installMode: "replace_current",
    });
    log.push({ step: "owner commands OTA deployment", result: deployment });

    const heartbeat = await request("POST", `/devices/${deviceId}/heartbeat`, {
      firmwareVersion: "0.1.0-local",
      batteryPercent: 91,
      network: "wifi",
      installedApps: [],
      activeDeploymentId: null,
    });
    log.push({ step: "device heartbeat receives pending command", result: heartbeat });

    const command = heartbeat.pendingCommands.find(
      (item) => item.deploymentId === deployment.deploymentId,
    );
    if (!command) {
      throw new Error("device did not receive OTA deployment command");
    }

    const installReport = await request(
      "POST",
      `/devices/${deviceId}/deployments/${deployment.deploymentId}/report`,
      {
        status: "installed",
        appId: job.app.appId,
        message: "simulator verified manifest, wrote app slot, and activated app",
      },
    );
    log.push({ step: "device reports install success", result: installReport });

    const finalHeartbeat = await request("POST", `/devices/${deviceId}/heartbeat`, {
      firmwareVersion: "0.1.0-local",
      batteryPercent: 90,
      network: "wifi",
      installedApps: [job.app.appId],
      activeDeploymentId: deployment.deploymentId,
    });
    log.push({ step: "device reports installed app", result: finalHeartbeat });

    const summary = {
      ok: true,
      ownerId,
      deviceId,
      appId: job.app.appId,
      artifactId: job.artifact.artifactId,
      deploymentId: deployment.deploymentId,
      pairingCode: pairing.pairingCode,
      installedApp: job.app.name,
      packageDigest: artifact.packageDigest,
      steps: log,
    };

    fs.mkdirSync(OUT_DIR, { recursive: true });
    fs.writeFileSync(path.join(OUT_DIR, "latest-run.json"), JSON.stringify(summary, null, 2));
    fs.writeFileSync(
      path.join(OUT_DIR, "generated-manifest.json"),
      JSON.stringify(artifact.manifest, null, 2),
    );

    console.log("Cheekoai local product loop: PASS");
    console.log(`Device: ${deviceId}`);
    console.log(`Pairing code: ${pairing.pairingCode}`);
    console.log(`Generated app: ${job.app.name} (${job.app.appId})`);
    console.log(`Deployment: ${deployment.deploymentId} installed`);
    console.log(`Digest: ${artifact.packageDigest}`);
    console.log("Artifacts:");
    console.log(`- ${path.relative(ROOT, path.join(OUT_DIR, "latest-run.json"))}`);
    console.log(`- ${path.relative(ROOT, path.join(OUT_DIR, "generated-manifest.json"))}`);
  } finally {
    await new Promise((resolve) => server.close(resolve));
  }
}

run().catch(async (error) => {
  console.error("Cheekoai local product loop: FAIL");
  console.error(error.stack || error.message);
  if (server.listening) {
    await new Promise((resolve) => server.close(resolve));
  }
  process.exitCode = 1;
});
