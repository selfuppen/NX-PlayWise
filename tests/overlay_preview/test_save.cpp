#include "renderer.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>

static void require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}

static std::string read(const std::filesystem::path &path)
{
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

static void write(const std::filesystem::path &path, const std::string &data)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(data.data(), static_cast<std::streamsize>(data.size()));
    require(stream.good(), "cannot prepare save fixture");
}

int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    try {
        const auto directory = std::filesystem::path(argv[2]);
        std::filesystem::create_directories(directory);
        const auto path = directory / "save-test.ppm";
        tsl::gfx::Renderer renderer(argv[1]);
        renderer.save(path.string());
        const auto original = read(path);
        require(original.size() == std::string("P6\n448 720\n255\n").size() + renderer.pixels.size(),
            "first save must contain the complete PPM");
        const auto sentinel = std::filesystem::file_time_type::clock::now() - std::chrono::hours(48);
        std::filesystem::last_write_time(path, sentinel);
        const auto preserved = std::filesystem::last_write_time(path);
        renderer.save(path.string());
        require(read(path) == original && std::filesystem::last_write_time(path) == preserved,
            "unchanged pixels must preserve bytes and mtime");

        renderer.pixels.back() ^= 1;
        renderer.save(path.string());
        const auto changed = read(path);
        require(changed != original && std::filesystem::last_write_time(path) != preserved,
            "a changed final pixel must replace the old image");
        for (const auto &broken : {changed.substr(0, changed.size() - 1), changed + "x",
                std::string("X") + changed.substr(1)}) {
            write(path, broken);
            renderer.save(path.string());
            require(read(path) == changed, "truncated, trailing or invalid-header files must be repaired");
        }
        bool failed = false;
        try {
            renderer.save((directory / "missing-parent/save-test.ppm").string());
        } catch (const std::runtime_error &) {
            failed = true;
        }
        require(failed, "an unwritable destination must report failure");
        std::filesystem::remove(path);
        std::puts("PASS: Overlay preview bytes/mtime reuse, changed pixels, malformed files and save failure");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
