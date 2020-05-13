ifeq (,$(CONFIGURATION))
	CONFIGURATION := release
endif

engine: makefile-gen
	@make -f makefile-gen GameEngine CONFIGURATION=$(CONFIGURATION)
	@rm makefile-gen
enginecore: makefile-gen
	@make -f makefile-gen TARGET_GameEngineCore_ CONFIGURATION=$(CONFIGURATION)

makefile-gen: build/makefilegen Builder/engine.proj FORCE
	./build/makefilegen ./Builder/engine.proj

FORCE:

# Build makefilegen
MAKEFILEGEN_DEP = CoreLib/LibString.cpp CoreLib/LibIO.cpp CoreLib/Stream.cpp CoreLib/Tokenizer.cpp CoreLib/TextIO.cpp
build/makefilegen: Builder/builder.cpp | build_dir
	@$(CXX) Builder/builder.cpp $(MAKEFILEGEN_DEP) -g -o $@ -std=c++14

build_dir:
	@mkdir -p build

clean:
	@rm -rf build
	@rm makefile-gen
.PHONY: build_dir