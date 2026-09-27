#pragma once
#include <string>
#include <cctype>

// Returns the last tqdm percentage ("NN%|") in demucs output, or -1 if none.
inline int parseDemucsProgress (const std::string& output)
{
    for (auto pos = output.rfind ("%|"); pos != std::string::npos && pos > 0;
         pos = output.rfind ("%|", pos - 1))
    {
        auto start = pos;
        while (start > 0 && std::isdigit ((unsigned char) output[start - 1]))
            --start;
        if (start < pos)
            return std::stoi (output.substr (start, pos - start));
    }
    return -1;
}
