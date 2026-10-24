import Foundation

/// Transport progress, not a guess based on elapsed time or HTTP headers.
enum ModelResponseProgress: Equatable { case waiting, receiving }

@MainActor protocol ModelProgressReporting: AnyObject {
    var onProgress: ((ModelResponseProgress) -> Void)? { get set }
}
