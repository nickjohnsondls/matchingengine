# CMake generated Testfile for 
# Source directory: /Users/maryjojohnson/Documents/matchingengine
# Build directory: /Users/maryjojohnson/Documents/matchingengine/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[OrderTests]=] "/Users/maryjojohnson/Documents/matchingengine/build/bin/test_order")
set_tests_properties([=[OrderTests]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/maryjojohnson/Documents/matchingengine/CMakeLists.txt;51;add_test;/Users/maryjojohnson/Documents/matchingengine/CMakeLists.txt;0;")
add_test([=[QueueTests]=] "/Users/maryjojohnson/Documents/matchingengine/build/bin/test_queues")
set_tests_properties([=[QueueTests]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/maryjojohnson/Documents/matchingengine/CMakeLists.txt;60;add_test;/Users/maryjojohnson/Documents/matchingengine/CMakeLists.txt;0;")
add_test([=[OrderBookTests]=] "/Users/maryjojohnson/Documents/matchingengine/build/bin/test_orderbook")
set_tests_properties([=[OrderBookTests]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/maryjojohnson/Documents/matchingengine/CMakeLists.txt;70;add_test;/Users/maryjojohnson/Documents/matchingengine/CMakeLists.txt;0;")
add_test([=[MatchingEngineTests]=] "/Users/maryjojohnson/Documents/matchingengine/build/bin/test_matching_engine")
set_tests_properties([=[MatchingEngineTests]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/maryjojohnson/Documents/matchingengine/CMakeLists.txt;80;add_test;/Users/maryjojohnson/Documents/matchingengine/CMakeLists.txt;0;")
