#include "Config.h"
#include "Pipeline.h"
#include "Logger.h"
#include "Utils.h"

#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    vcap::Config cfg;
    if (!cfg.parse(argc, argv))
        return EXIT_FAILURE;

    if (cfg.verbose)
        vcap::Logger::instance().setLevel(vcap::LogLevel::DEBUG);

    // Print startup banner
    fprintf(stdout,
        "\n"
        "  ██╗   ██╗ ██████╗ █████╗ ██████╗ \n"
        "  ██║   ██║██╔════╝██╔══██╗██╔══██╗\n"
        "  ██║   ██║██║     ███████║██████╔╝\n"
        "  ╚██╗ ██╔╝██║     ██╔══██║██╔═══╝ \n"
        "   ╚████╔╝ ╚██████╗██║  ██║██║     \n"
        "    ╚═══╝   ╚═════╝╚═╝  ╚═╝╚═╝     \n"
        "  Video Capture — layered engine v1.0\n\n");

    fprintf(stdout, "  URL    : %s\n", cfg.url.c_str());
    fprintf(stdout, "  Output : %s\n", cfg.outputPath.c_str());
    fprintf(stdout, "  Layers : %s%s%s\n\n",
            cfg.enableCdp    ? "[CDP] "    : "",
            cfg.enableYtDlp  ? "[yt-dlp] " : "",
            cfg.enableScreen ? "[Screen]"  : "");

    vcap::Pipeline pipeline(cfg);

    pipeline.onStatus([](const vcap::PipelineStatus& s) {
        // Progress already printed by reportStatus — no-op here unless
        // you want to hook into a GUI notification system
        (void)s;
    });

    bool ok = pipeline.run();

    if (ok) {
        fprintf(stdout, "\n✓ Saved: %s\n\n", cfg.outputPath.c_str());
        return EXIT_SUCCESS;
    } else {
        fprintf(stderr, "\n✗ Capture failed. Check logs above.\n\n");
        return EXIT_FAILURE;
    }
}
