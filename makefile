ifeq (,$(CONFIGURATION))
	CONFIGURATION := release
endif

engine: makefile-gen ExternalLibs/Slang/bin/linux-x64
	@$(MAKE) -f makefile-gen GameEngine CONFIGURATION=$(CONFIGURATION)
	@rm makefile-gen

makefile-gen: build/makefilegen MakefileGen/engine.proj FORCE
	./build/makefilegen ./MakefileGen/engine.proj

FORCE:

# Build makefilegen
MAKEFILEGEN_DEP = CoreLib/LibString.cpp CoreLib/LibIO.cpp CoreLib/Stream.cpp CoreLib/Tokenizer.cpp CoreLib/TextIO.cpp
build/makefilegen: MakefileGen/builder.cpp | build_dir
	@$(CXX) MakefileGen/builder.cpp $(MAKEFILEGEN_DEP) -g -o $@ -std=c++14

SLANG_VERSION=0.12.4
ExternalLibs/Slang ExternalLibs/Slang/bin/linux-x64 :
	@echo "Downloading external libraries..."
	wget "https://github.com/csyonghe/SpireMiniEngineExtBinaries/raw/master/Slang-$(SLANG_VERSION).tar.gz" -O Slang.tar.gz
	@mkdir -p ExternalLibs/
	@tar xvzf Slang.tar.gz -C ExternalLibs/
	@rm -f Slang.tar.gz


build_dir:
	@mkdir -p build

clean:
	@rm -rf build
	-@rm -f makefile-gen
.PHONY: build_dir