cmake_minimum_required(VERSION 3.2)

catapult_add_gtest_dependencies()
catapult_library_target(tests.catapult.int.node.test)
target_link_libraries(tests.catapult.int.node.test tests.catapult.test.local)
