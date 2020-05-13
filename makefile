ifeq (,$(CONFIGURATION))
	CONFIGURATION := release
endif

engine: makefile-gen
	@$(MAKE) -f makefile-gen GameEngine CONFIGURATION=$(CONFIGURATION)
	@rm makefile-gen
enginecore: makefile-gen
	@$(MAKE) -f makefile-gen TARGET_GameEngineCore_ CONFIGURATION=$(CONFIGURATION)

makefile-gen: build/makefilegen MakefileGen/engine.proj FORCE
	./build/makefilegen ./MakefileGen/engine.proj

FORCE:

# Build makefilegen
MAKEFILEGEN_DEP = CoreLib/LibString.cpp CoreLib/LibIO.cpp CoreLib/Stream.cpp CoreLib/Tokenizer.cpp CoreLib/TextIO.cpp
build/makefilegen: MakefileGen/builder.cpp | build_dir
	@$(CXX) MakefileGen/builder.cpp $(MAKEFILEGEN_DEP) -g -o $@ -std=c++14

build_dir:
	@mkdir -p build

clean:
	@rm -rf build
	-@rm -f makefile-gen
.PHONY: build_dir