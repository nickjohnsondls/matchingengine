# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 4.0

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:

#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:

# Disable VCS-based implicit rules.
% : %,v

# Disable VCS-based implicit rules.
% : RCS/%

# Disable VCS-based implicit rules.
% : RCS/%,v

# Disable VCS-based implicit rules.
% : SCCS/s.%

# Disable VCS-based implicit rules.
% : s.%

.SUFFIXES: .hpux_make_needs_suffix_list

# Command-line flag to silence nested $(MAKE).
$(VERBOSE)MAKESILENT = -s

#Suppress display of executed commands.
$(VERBOSE).SILENT:

# A target that is always out of date.
cmake_force:
.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

# The shell in which to execute make rules.
SHELL = /bin/sh

# The CMake executable.
CMAKE_COMMAND = /usr/local/bin/cmake

# The command to remove a file.
RM = /usr/local/bin/cmake -E rm -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /Users/maryjojohnson/Documents/matchingengine

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /Users/maryjojohnson/Documents/matchingengine/build

# Include any dependencies generated for this target.
include CMakeFiles/network_demo.dir/depend.make
# Include any dependencies generated by the compiler for this target.
include CMakeFiles/network_demo.dir/compiler_depend.make

# Include the progress variables for this target.
include CMakeFiles/network_demo.dir/progress.make

# Include the compile flags for this target's objects.
include CMakeFiles/network_demo.dir/flags.make

CMakeFiles/network_demo.dir/codegen:
.PHONY : CMakeFiles/network_demo.dir/codegen

CMakeFiles/network_demo.dir/examples/network_demo.cpp.o: CMakeFiles/network_demo.dir/flags.make
CMakeFiles/network_demo.dir/examples/network_demo.cpp.o: /Users/maryjojohnson/Documents/matchingengine/examples/network_demo.cpp
CMakeFiles/network_demo.dir/examples/network_demo.cpp.o: CMakeFiles/network_demo.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --progress-dir=/Users/maryjojohnson/Documents/matchingengine/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building CXX object CMakeFiles/network_demo.dir/examples/network_demo.cpp.o"
	/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -MD -MT CMakeFiles/network_demo.dir/examples/network_demo.cpp.o -MF CMakeFiles/network_demo.dir/examples/network_demo.cpp.o.d -o CMakeFiles/network_demo.dir/examples/network_demo.cpp.o -c /Users/maryjojohnson/Documents/matchingengine/examples/network_demo.cpp

CMakeFiles/network_demo.dir/examples/network_demo.cpp.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Preprocessing CXX source to CMakeFiles/network_demo.dir/examples/network_demo.cpp.i"
	/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /Users/maryjojohnson/Documents/matchingengine/examples/network_demo.cpp > CMakeFiles/network_demo.dir/examples/network_demo.cpp.i

CMakeFiles/network_demo.dir/examples/network_demo.cpp.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Compiling CXX source to assembly CMakeFiles/network_demo.dir/examples/network_demo.cpp.s"
	/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /Users/maryjojohnson/Documents/matchingengine/examples/network_demo.cpp -o CMakeFiles/network_demo.dir/examples/network_demo.cpp.s

# Object files for target network_demo
network_demo_OBJECTS = \
"CMakeFiles/network_demo.dir/examples/network_demo.cpp.o"

# External object files for target network_demo
network_demo_EXTERNAL_OBJECTS =

bin/network_demo: CMakeFiles/network_demo.dir/examples/network_demo.cpp.o
bin/network_demo: CMakeFiles/network_demo.dir/build.make
bin/network_demo: lib/libmicromatch_core.a
bin/network_demo: CMakeFiles/network_demo.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --bold --progress-dir=/Users/maryjojohnson/Documents/matchingengine/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Linking CXX executable bin/network_demo"
	$(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/network_demo.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
CMakeFiles/network_demo.dir/build: bin/network_demo
.PHONY : CMakeFiles/network_demo.dir/build

CMakeFiles/network_demo.dir/clean:
	$(CMAKE_COMMAND) -P CMakeFiles/network_demo.dir/cmake_clean.cmake
.PHONY : CMakeFiles/network_demo.dir/clean

CMakeFiles/network_demo.dir/depend:
	cd /Users/maryjojohnson/Documents/matchingengine/build && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /Users/maryjojohnson/Documents/matchingengine /Users/maryjojohnson/Documents/matchingengine /Users/maryjojohnson/Documents/matchingengine/build /Users/maryjojohnson/Documents/matchingengine/build /Users/maryjojohnson/Documents/matchingengine/build/CMakeFiles/network_demo.dir/DependInfo.cmake "--color=$(COLOR)"
.PHONY : CMakeFiles/network_demo.dir/depend

