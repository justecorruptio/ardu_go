SDK := $(shell xcrun --show-sdk-path 2>/dev/null || echo /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk)
CXXFLAGS := -isysroot $(SDK) -std=c++17 -O2 -Wall -Wno-unused-function -I.

all: play play_gui harness test_lnd test_ladder

test_lnd: test_lnd.cpp ../ai.cpp ../game.cpp ../*.h Arduboy2.h
	clang++ $(CXXFLAGS) test_lnd.cpp -o test_lnd

test_ladder: test_ladder.cpp ../ai.cpp ../game.cpp ../*.h Arduboy2.h
	clang++ $(CXXFLAGS) test_ladder.cpp -o test_ladder

play: play.cpp ../ai.cpp ../game.cpp ../*.h Arduboy2.h
	clang++ $(CXXFLAGS) play.cpp -o play

play_gui: play_gui.mm ../ai.cpp ../game.cpp ../*.h Arduboy2.h
	clang++ $(CXXFLAGS) -fobjc-arc play_gui.mm -o play_gui -framework Cocoa

harness: harness.cpp ../ai.cpp ../game.cpp ../*.h Arduboy2.h
	clang++ $(CXXFLAGS) harness.cpp -o harness

clean:
	rm -f play play_gui harness test_lnd test_ladder game_*.sgf

.PHONY: all clean
