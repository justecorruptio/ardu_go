SDK := $(shell xcrun --show-sdk-path 2>/dev/null || echo /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk)
CXXFLAGS := -isysroot $(SDK) -std=c++17 -O2 -Wall -Wno-unused-function -I.

all: play_gui harness test_lnd test_ladder

test_lnd: test_lnd.cpp ../ai.cpp ../game.cpp ../*.h Arduboy2.h
	clang++ $(CXXFLAGS) test_lnd.cpp -o test_lnd

test_ladder: test_ladder.cpp ../ai.cpp ../game.cpp ../*.h Arduboy2.h
	clang++ $(CXXFLAGS) test_ladder.cpp -o test_ladder

play_gui: play_gui.mm ../ai.cpp ../game.cpp ../*.h Arduboy2.h
	clang++ $(CXXFLAGS) -DNNOPEN -DNN_DEVICE_TIER -DNN_CORE_TIER -fobjc-arc play_gui.mm -o play_gui -framework Cocoa

harness: harness.cpp ../ai.cpp ../game.cpp ../*.h Arduboy2.h
	clang++ $(CXXFLAGS) harness.cpp -o harness

# movecmp had NO rule: `make movecmp` silently used make's implicit
# %.cpp rule, whose only prerequisite is movecmp.cpp -- so it never
# rebuilt when ai.cpp changed and byte-identity checks compared a stale
# binary against itself. Keep every verification target in this list.
movecmp: movecmp.cpp ../ai.cpp ../game.cpp ../*.h Arduboy2.h
	clang++ $(CXXFLAGS) movecmp.cpp -o movecmp

clean:
	rm -f play_gui harness test_lnd test_ladder game_*.sgf

.PHONY: all clean
