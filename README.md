# build.h

**build.h** is header-only build system for C

## Usage

```c

#if 0
cc -o /tmp/example example.c && /tmp/example ; exit $?
#endif

#include "build.h"

static Target* hello_lib(void);
static Target* main_exe(void);

int main(void)
{
    Target* project = dependency_group("project");
    add_dependency(project, main_exe());
    build(project);
}

static Target* main_exe(void)
{
    Target* target = executable("hello");
    add_source(target, "./example/main.c");
    add_include_directory(target, "./example");
    add_dependency(target, hello_lib());
    return target;
}

static Target* hello_lib(void)
{
    Target* target = static_library("LibHello");
    add_source(target, "./example/LibHello/hello.c");
    return target;
}


```

## Build instructions

### Build:

```sh

./example.c

```

### Run:

```sh

./build/main

```

