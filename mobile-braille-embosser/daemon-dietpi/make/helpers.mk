define newline


endef

cpp_to_obj = $(patsubst %.cpp,%.o,$(1))

# module_objs(dir, exclude.cpp, ...) — all .cpp in dir except listed basenames
module_objs = $(call cpp_to_obj,$(filter-out $(foreach e,$(2),$(1)/$(e)),$(wildcard $(1)/*.cpp)))
