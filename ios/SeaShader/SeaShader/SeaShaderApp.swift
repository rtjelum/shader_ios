import SwiftUI

@main
struct SeaShaderApp: App {
    var body: some Scene {
        WindowGroup {
            ShaderView()
                .ignoresSafeArea()
                .statusBarHidden()
                .persistentSystemOverlays(.hidden)
        }
    }
}
