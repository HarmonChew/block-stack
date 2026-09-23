#include "blocks/game.hpp"
#include "blocks/replay.hpp"

#include <filesystem>
#include <stdexcept>

void test_replay() {
    blocks::Config config;
    config.start_level = 18;
    config.seed = 12345;
    blocks::Game game(config);
    blocks::Replay replay;
    replay.config = config;
    replay.hash_interval = 17;
    for (int i = 0; i < 300; ++i) {
        const blocks::InputFrame input = (i % 31 == 0) ? blocks::RotateCW : ((i % 7) < 3 ? blocks::Left : 0);
        game.tick(input);
        replay.append(input, game);
    }
    const auto path = std::filesystem::temp_directory_path() / "blocks-test.rep";
    replay.save(path.string());
    const auto loaded = blocks::Replay::load(path.string());
    std::filesystem::remove(path);
    if (!blocks::verify_replay(loaded).valid)
        throw std::runtime_error("saved replay did not verify");
    auto corrupted = loaded;
    corrupted.inputs[45] ^= blocks::Right;
    if (blocks::verify_replay(corrupted).valid)
        throw std::runtime_error("corrupted replay verified");
}
