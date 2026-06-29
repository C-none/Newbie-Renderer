import nr.pipeline;
import std;

int main(int argc, char** argv)
{
    auto args = std::span<char*>{};
    if (argc > 1)
    {
        args = std::span<char*>{argv + 1, static_cast<std::size_t>(argc - 1)};
    }

    return nr::pipeline::runViewerFromCommandLine(args);
}
