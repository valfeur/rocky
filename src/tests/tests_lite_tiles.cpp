/* rocky
 * Copyright 2026 Pelican Mapping
 * MIT License
 */
//! Phase 3 (ROCKY_LITE_SPEC.md) validation: a local-directory XYZ image tile
//! with zero network, and a real plain-http local tile server (via the
//! vendored cpp-httplib, no TLS) -- exercising the actual image decode
//! (vsgXchange/stb) and Terrain-RGB/Mapbox elevation decode paths end to
//! end, not just confirming the build compiles with GDAL/PROJ/curl off.
#include "catch.hpp"

#include <rocky/rocky.h>
#include <rocky/vsg/VSGContext.h>

#include <filesystem>
#include <chrono>
#include <thread>

// vsgXchange vendors its own internal copy of stb_image_write.h with
// external linkage (src/stbi/stbi.cpp, STB_IMAGE_WRITE_IMPLEMENTATION with
// no STBIW_STATIC); when it's statically linked (non-ROCKY_LITE
// configurations may build vsgXchange as a static lib), defining the same
// externally-linked symbols here again would be a duplicate-symbol link
// error. STBIW_STATIC makes every stb_image_write function internal to
// this translation unit, so there's no possible clash either way.
#define STBIW_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#ifdef ROCKY_HAS_HTTPLIB
#include <httplib.h>
#endif

using namespace ROCKY_NAMESPACE;

namespace
{
    //! Writes a tiny uniformly-colored RGBA PNG to disk for use as a test tile.
    void writeSolidPNG(const std::filesystem::path& path, int w, int h,
        unsigned char r, unsigned char g, unsigned char b, unsigned char a = 255)
    {
        std::filesystem::create_directories(path.parent_path());
        std::vector<unsigned char> pixels((std::size_t)w * h * 4);
        for (std::size_t i = 0; i < pixels.size(); i += 4)
        {
            pixels[i + 0] = r; pixels[i + 1] = g; pixels[i + 2] = b; pixels[i + 3] = a;
        }
        REQUIRE(stbi_write_png(path.string().c_str(), w, h, 4, pixels.data(), w * 4) != 0);
    }

    //! Reads a pixel as plain normalized [0,1] bytes, bypassing sRGB
    //! linearization -- the decoded tile may come back tagged as an SRGB
    //! pixel format (a color-management choice, not a decode bug), and
    //! Image::read() linearizes those; viewAs() reinterprets the same bytes
    //! as UNORM first (same technique ElevationLayer::decodeRGB uses to get
    //! at raw RGB-encoded values).
    Image::Pixel readRawPixel(const std::shared_ptr<Image>& image, unsigned s, unsigned t)
    {
        auto unormFormat =
            image->pixelFormat() == Image::R8_SRGB ? Image::R8_UNORM :
            image->pixelFormat() == Image::R8G8_SRGB ? Image::R8G8_UNORM :
            image->pixelFormat() == Image::R8G8B8_SRGB ? Image::R8G8B8_UNORM :
            image->pixelFormat() == Image::R8G8B8A8_SRGB ? Image::R8G8B8A8_UNORM :
            image->pixelFormat();
        Image view = image->viewAs(unormFormat);
        return view.read(s, t);
    }

    //! Mirrors ElevationLayer::decodeRGB's MapboxRGB formula exactly
    //! (ElevationLayer.cpp), which operates on normalized [0,1] pixel
    //! components -- so this is what the decode actually computes for a
    //! given set of raw bytes, not an idealized textbook formula.
    float mapboxHeight(unsigned char r, unsigned char g, unsigned char b)
    {
        float pr = r / 255.0f, pg = g / 255.0f, pb = b / 255.0f;
        return -10000.f + ((pr * 256.0f * 256.0f + pg * 256.0f + pb) * 256.0f * 0.1f);
    }

    //! A unique temp directory for one test's tile fixtures, removed on destruction.
    struct TempTileDir
    {
        std::filesystem::path dir;

        TempTileDir()
        {
            auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            dir = std::filesystem::temp_directory_path() /
                ("rocky_lite_tiles_" + std::to_string(stamp));
            std::filesystem::create_directories(dir);
        }

        ~TempTileDir()
        {
            std::error_code ec;
            std::filesystem::remove_all(dir, ec);
        }
    };

    //! A minimal VSGContext sufficient to exercise IOOptions services
    //! (image decode, HTTP) without an actual window or GPU device. The
    //! caller must keep the returned singleton alive for as long as the
    //! VSGContext* it holds is used (same pattern as the rest of tests.cpp).
    auto makeTestContext()
    {
        auto contextSingleton = VSGContextFactory::create(nullptr);
        REQUIRE(contextSingleton.get()->status.ok());
        return contextSingleton;
    }
}

TEST_CASE("Lite tiles: local-directory XYZ image, no network", "[lite][tiles]")
{
    TempTileDir fixture;

    const unsigned char R = 10, G = 200, B = 30;
    writeSolidPNG(fixture.dir / "0" / "0" / "0.png", 4, 4, R, G, B);

    auto layer = TMSImageLayer::create();
    layer->uri = (fixture.dir / "{z}/{x}/{y}.png").generic_string();
    layer->format = "png";
    layer->profile = Profile("spherical-mercator");

    auto contextSingleton = makeTestContext();
    auto context = contextSingleton.get();

    auto openResult = layer->open(context->io);
    REQUIRE(openResult.ok());

    TileKey key(0, 0, 0, layer->profile);
    auto tile = layer->createTile(key, context->io);
    REQUIRE(tile.ok());
    REQUIRE(tile->valid());

    auto pixel = readRawPixel(tile->image(), 1, 1);
    CHECK(glm::epsilonEqual(pixel.r, R / 255.0f, 0.01f));
    CHECK(glm::epsilonEqual(pixel.g, G / 255.0f, 0.01f));
    CHECK(glm::epsilonEqual(pixel.b, B / 255.0f, 0.01f));

    layer->close();
}

TEST_CASE("Lite tiles: local-directory Terrain-RGB (Mapbox) elevation, no network", "[lite][tiles][elevation]")
{
    TempTileDir fixture;

    const unsigned char R = 128, G = 64, B = 32;
    writeSolidPNG(fixture.dir / "0" / "0" / "0.png", 4, 4, R, G, B);

    auto layer = TMSElevationLayer::create();
    layer->uri = (fixture.dir / "{z}/{x}/{y}.png").generic_string();
    layer->format = "png";
    layer->profile = Profile("spherical-mercator");
    layer->encoding = ElevationLayer::Encoding::MapboxRGB;

    auto contextSingleton = makeTestContext();
    auto context = contextSingleton.get();

    auto openResult = layer->open(context->io);
    REQUIRE(openResult.ok());

    TileKey key(0, 0, 0, layer->profile);
    auto tile = layer->createTile(key, context->io);
    INFO("createTile error: " << (tile.failed() ? tile.error().message : "(none)"));
    REQUIRE(tile.ok());
    REQUIRE(tile->valid());

    GeoHeightfield hf(tile.value());
    auto center = hf.read(0.0, 0.0); // center of the spherical-mercator world tile
    REQUIRE(center.ok());
    CHECK(glm::epsilonEqual(center.value(), mapboxHeight(R, G, B), 1.0f));

    layer->close();
}

#ifdef ROCKY_HAS_HTTPLIB
namespace
{
    //! A minimal local HTTP server (no TLS) serving files from a directory,
    //! using the same vendored cpp-httplib the lite build links against.
    struct LocalTileServer
    {
        httplib::Server server;
        std::thread thread;
        int port = 0;

        explicit LocalTileServer(const std::filesystem::path& root)
        {
            server.set_mount_point("/", root.string());
            port = server.bind_to_any_port("127.0.0.1");
            REQUIRE(port > 0);
            thread = std::thread([this] { server.listen_after_bind(); });
        }

        std::string baseURL() const
        {
            return "http://127.0.0.1:" + std::to_string(port) + "/";
        }

        ~LocalTileServer()
        {
            server.stop();
            if (thread.joinable())
                thread.join();
        }
    };
}

TEST_CASE("Lite tiles: plain-http local tile server (no HTTPS)", "[lite][tiles][http]")
{
    TempTileDir fixture;

    const unsigned char R = 250, G = 40, B = 5;
    writeSolidPNG(fixture.dir / "0" / "0" / "0.png", 4, 4, R, G, B);

    LocalTileServer httpServer(fixture.dir);

    auto layer = TMSImageLayer::create();
    layer->uri = httpServer.baseURL() + "{z}/{x}/{y}.png";
    layer->format = "png";
    layer->profile = Profile("spherical-mercator");

    auto contextSingleton = makeTestContext();
    auto context = contextSingleton.get();

    auto openResult = layer->open(context->io);
    REQUIRE(openResult.ok());

    TileKey key(0, 0, 0, layer->profile);
    auto tile = layer->createTile(key, context->io);
    REQUIRE(tile.ok());
    REQUIRE(tile->valid());

    auto pixel = readRawPixel(tile->image(), 1, 1);
    CHECK(glm::epsilonEqual(pixel.r, R / 255.0f, 0.01f));
    CHECK(glm::epsilonEqual(pixel.g, G / 255.0f, 0.01f));
    CHECK(glm::epsilonEqual(pixel.b, B / 255.0f, 0.01f));

    layer->close();
}

TEST_CASE("Lite tiles: a missing tile over plain http fails gracefully", "[lite][tiles][http]")
{
    TempTileDir fixture; // intentionally left empty -- no tiles written

    LocalTileServer httpServer(fixture.dir);

    auto layer = TMSImageLayer::create();
    layer->uri = httpServer.baseURL() + "{z}/{x}/{y}.png";
    layer->format = "png";
    layer->profile = Profile("spherical-mercator");

    auto contextSingleton = makeTestContext();
    auto context = contextSingleton.get();

    REQUIRE(layer->open(context->io).ok());

    TileKey key(0, 0, 0, layer->profile);
    auto tile = layer->createTile(key, context->io);
    CHECK(tile.failed());

    layer->close();
}

TEST_CASE("Lite tiles: an https:// tile source fails with a clear TLS message", "[lite][tiles][http]")
{
    if (URI::supportsHTTPS())
        return; // this case is specifically about the no-TLS configuration

    auto layer = TMSImageLayer::create();
    layer->uri = "https://127.0.0.1:1/{z}/{x}/{y}.png"; // never actually connected to
    layer->format = "png";
    layer->profile = Profile("spherical-mercator");

    auto contextSingleton = makeTestContext();
    auto context = contextSingleton.get();
    REQUIRE(layer->open(context->io).ok()); // open doesn't touch the network when profile is preset

    TileKey key(0, 0, 0, layer->profile);
    auto tile = layer->createTile(key, context->io);
    REQUIRE(tile.failed());
    CHECK(tile.error().message == "TLS not supported in rocky-lite");

    layer->close();
}
#endif // ROCKY_HAS_HTTPLIB
