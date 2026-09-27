#include "../Source/DemucsProgress.h"
#include <cassert>
#include <fstream>
#include <sstream>
#include <cstdio>

int main()
{
    assert (parseDemucsProgress ("") == -1);
    assert (parseDemucsProgress ("Separating track song.mp3\n") == -1);
    assert (parseDemucsProgress ("  0%|          | 0.0/58.5 [00:00<?, ?seconds/s]") == 0);
    assert (parseDemucsProgress (" 12%|#  | 7/58\r 45%|####  | 26/58 [00:10<00:12]") == 45);
    assert (parseDemucsProgress ("100%|##########| 58.5/58.5 [00:22<00:00]\n") == 100);
    assert (parseDemucsProgress (" 45%|####  | 26/58\rrate 50% done") == 45); // "%" without "|" ignored
    { auto v = parseDemucsProgress ("99999999999999999999%|"); assert (v >= 0 && v <= 999); }

    // real captured output (Task 1, Step 2) must end at 100
    std::ifstream f (SAMPLE_OUTPUT_PATH, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    assert (parseDemucsProgress (ss.str()) == 100);

    std::puts ("progress_test OK");
    return 0;
}
