ifeq (,$(CONFIGURATION))
	CONFIGURATION := release
endif

PLATFORM := $(shell uname -s | tr '[:upper:]' '[:lower:]')
ARCHITECTURE := $(shell uname -p)

ifeq (,$(CONFIGURATION))
	CONFIGURATION := release
endif

TARGET := $(PLATFORM)-$(ARCHITECTURE)

ENGINE := build/$(TARGET)/$(CONFIGURATION)/GameEngine
$(ENGINE): makefile-gen ExternalLibs/Slang/bin/linux-x64 build/depinstall
	@$(MAKE) -f makefile-gen GameEngine CONFIGURATION=$(CONFIGURATION)
	@rm makefile-gen

makefile-gen: build/makefilegen MakefileGen/engine.proj FORCE
	./build/makefilegen ./MakefileGen/engine.proj

FORCE:

# Build makefilegen
MAKEFILEGEN_DEP = CoreLib/LibString.cpp CoreLib/LibIO.cpp CoreLib/Stream.cpp CoreLib/Tokenizer.cpp CoreLib/TextIO.cpp
build/makefilegen: MakefileGen/builder.cpp | build_dir
	@$(CXX) MakefileGen/builder.cpp $(MAKEFILEGEN_DEP) -g -o $@ -std=c++17

SLANG_VERSION=0.15.10
ExternalLibs/Slang ExternalLibs/Slang/bin/linux-x64 :
	@echo "Downloading external libraries..."
	wget "https://github.com/csyonghe/SpireMiniEngineExtBinaries/raw/master/Slang-$(SLANG_VERSION).tar.gz" -O Slang.tar.gz
	@mkdir -p ExternalLibs/Slang/
	@tar xvzf Slang.tar.gz -C ExternalLibs/Slang/
	@rm -f Slang.tar.gz

build/depinstall: build_dir
	@echo "#include <X11/Xlib.h>" > build/x11test.cpp
	@echo "int main(){return 0;}" >> build/x11test.cpp
	@if g++ -c build/x11test.cpp -o build/x11test 2> $@ ; then echo "Xlib detected" ; fi
	@if ! test -f build/x11test ; then\
		echo "Required package libx11-dev not found, atempting to install...";\
		sudo apt-get install libx11-dev;\
	fi
	@rm -f build/x11test.cpp
	@rm -f build/x11test
	@touch $@

build_dir:
	@mkdir -p build

test: $(ENGINE)
	@bash run_test.sh $(ENGINE)
clean:
	@rm -rf build
	-@rm -f makefile-gen
.PHONY: build_dir clean test