#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CAT2(a, b) a##b
#define CAT(a, b) CAT2(a, b)

typedef char const* c_string;
typedef __SIZE_TYPE__ usize;

static inline int vec_append_impl(void** out_elements, usize* out_count, usize* out_capacity,
    usize element_size, void const* element, usize element_count)
{
    int result = 0;
    if (!*out_elements) {
        usize new_capacity = 8 + element_count;
        void* new_elements = calloc(new_capacity, element_size);
        if (!new_elements)
            return -1;
        *out_elements = new_elements;
        *out_capacity = new_capacity;
        *out_count = 0;
    }
    if ((*out_count + element_count) >= *out_capacity) {
        usize new_capacity = (*out_capacity * 2) + element_count;
        void* new_elements = realloc(*out_elements, new_capacity * element_size);
        if (!new_elements)
            return -1;
        *out_capacity = new_capacity;
        *out_elements = new_elements;
    }
    char* element_bytes = (char*)*out_elements;
    usize index = *out_count;
    void* slot = element_bytes + (element_size * index);
    memcpy(slot, element, element_size * element_count);
    *out_count += element_count;
    return 0;
}

#define vec_append(array, ...)                                                                  \
    ({                                                                                          \
        int result = 0;                                                                         \
        if (!(array)->elements) {                                                               \
            usize capacity = 8;                                                                 \
            void* elements = calloc(capacity, sizeof(*(array)->elements));                      \
            if (!elements) {                                                                    \
                result = -1;                                                                    \
                goto CAT(append_error_, __LINE__);                                              \
            }                                                                                   \
            (array)->elements = (__typeof(((array)->elements)))elements;                        \
            (array)->capacity = capacity;                                                       \
            (array)->count = 0;                                                                 \
        }                                                                                       \
        if ((array)->count >= (array)->capacity) {                                              \
            usize capacity = (array)->capacity * 2;                                             \
            void* elements = realloc((array)->elements, capacity * sizeof(*(array)->elements)); \
            if (!elements) {                                                                    \
                result = -1;                                                                    \
                goto CAT(append_error_, __LINE__);                                              \
            }                                                                                   \
            (array)->capacity = capacity;                                                       \
            (array)->elements = (__typeof(((array)->elements)))elements;                        \
        }                                                                                       \
        (array)->elements[(array)->count++] = (__VA_ARGS__);                                    \
        CAT(append_error_, __LINE__)                                                            \
            : result;                                                                           \
    })

#define vec_append_many(array, data, data_count)                                              \
    ({                                                                                        \
        __typeof(*(array)->elements) const* value = data;                                     \
        void** elements = (void**)&((array)->elements);                                       \
        vec_append_impl(elements, &(array)->count, &(array)->capacity, sizeof(*value), value, \
            data_count);                                                                      \
    })

typedef struct {
    char* elements;
    usize count;
    usize capacity;
} String;
static inline int string_append(String* dest, c_string other)
{
    usize count = strlen(other);
    return vec_append_many(dest, other, count);
}

typedef enum {
    TargetKind_Executable,
    TargetKind_StaticLibrary,
    TargetKind_Project,
} TargetKind;

typedef struct {
    c_string* elements;
    usize count;
    usize capacity;
} SourceList;

struct Target;
typedef struct {
    struct Target** elements;
    usize count;
    usize capacity;
} Dependencies;

typedef struct Target {
    SourceList sources;
    SourceList include_directories;
    Dependencies dependencies;
    c_string name;
    TargetKind kind;
} Target;

typedef struct {
    c_string name;
    c_string source;

    SourceList include_directories;
} CompileJob;

typedef struct {
    c_string name;
    SourceList objects;
    SourceList libraries;
    TargetKind kind;
} LinkJob;

typedef struct {
    struct {
        CompileJob* elements;
        usize count;
        usize capacity;
    } compile;
    struct {
        LinkJob* elements;
        usize count;
        usize capacity;
    } link;

    Target** elements;
    usize count;
    usize capacity;

    usize jobs;
    usize jobs_done;
} Schedule;

static inline Target* make_target(c_string name, TargetKind kind)
{
    Target target = {
        .name = name,
        .kind = kind,
    };
    Target* p = (Target*)malloc(sizeof(Target));
    *p = target;
    return p;
}

static inline Target* static_library(c_string name)
{
    return make_target(name, TargetKind_StaticLibrary);
}

static inline Target* executable(c_string name)
{
    return make_target(name, TargetKind_Executable);
}

static inline Target* dependency_group(c_string name)
{
    return make_target(name, TargetKind_Project);
}

static inline int add_source(Target* target, c_string source)
{
    return vec_append(&target->sources, source);
}

static inline int add_dependency(Target* target, Target* dep)
{
    return vec_append(&target->dependencies, dep);
}

static inline int add_include_directory(Target* target, c_string path)
{
    return vec_append(&target->include_directories, path);
}

static inline void schedule_build(Target* target, Schedule* schedule)
{
    for (usize i = 0; i < schedule->count; i++) {
        Target* a = schedule->elements[i];
        if (strcmp(target->name, a->name) == 0)
            return;
    }
    SourceList libraries = { 0 };
    for (usize i = 0; i < target->dependencies.count; i++) {
        Target* dep = target->dependencies.elements[i];
        schedule_build(dep, schedule);
        vec_append(&libraries, dep->name);
    }

    SourceList objects = { 0 };
    for (usize i = 0; i < target->sources.count; i++) {
        c_string source = target->sources.elements[i];
        char* dest = 0;
        asprintf(&dest, "%s.o", source);
        vec_append(&objects, dest);
        vec_append(&schedule->compile,
            (CompileJob) {
                .name = dest,
                .source = source,
                .include_directories = target->include_directories,
            });
        schedule->jobs++;
    }

    if (target->kind != TargetKind_Project) {
        char* name = 0;
        switch (target->kind) {
        case TargetKind_StaticLibrary: asprintf(&name, "lib%s.a", target->name); break;
        case TargetKind_Project: break;
        case TargetKind_Executable: name = (char*)target->name; break;
        }
        vec_append(&schedule->link,
            (LinkJob) {
                .name = name,
                .objects = objects,
                .libraries = libraries,
                .kind = target->kind,
            });
        schedule->jobs++;
    }
    vec_append(schedule, target);
}

static inline void compile_objects(Schedule* schedule)
{
    usize jobs = schedule->jobs;
    usize count = schedule->compile.count;
    for (usize i = 0; i < count; i++, schedule->jobs_done++) {
        CompileJob job = schedule->compile.elements[i];
        printf("\r\033[K[%zu/%zu] Compiling %s ", schedule->jobs_done + 1, jobs, job.name);
        fflush(0);

        String out = { 0 };
        string_append(&out, "mkdir -p `dirname ");
        string_append(&out, "./build/");
        string_append(&out, job.name);
        string_append(&out, "`");
        vec_append(&out, '\0');
        system(out.elements);

        String command = { 0 };
        string_append(&command, "ccache cc -c -o ./build/");
        string_append(&command, job.name);
        string_append(&command, " ");

        for (usize j = 0; j < job.include_directories.count; j++) {
            c_string dir = job.include_directories.elements[j];
            string_append(&command, "-I");
            string_append(&command, dir);
            string_append(&command, " ");
        }

        string_append(&command, job.source);
        vec_append(&command, '\0');
        if (system(command.elements) != 0)
            exit(1);
    }
}

static inline void link_targets(Schedule* schedule)
{
    usize jobs = schedule->jobs;
    usize count = schedule->link.count;
    for (usize i = 0; i < count; i++, schedule->jobs_done++) {
        LinkJob job = schedule->link.elements[i];
        printf("\r\033[K[%zu/%zu] Linking %s ", schedule->jobs_done + 1, jobs, job.name);
        fflush(0);

        String out = { 0 };
        string_append(&out, "mkdir -p `dirname ");
        string_append(&out, "./build/");
        string_append(&out, job.name);
        string_append(&out, "`");
        vec_append(&out, '\0');
        system(out.elements);

        String command = { 0 };

        if (job.kind == TargetKind_StaticLibrary) {
            string_append(&command, "ar -crs ./build/");
            string_append(&command, job.name);
            string_append(&command, " ");
            for (usize j = 0; j < job.objects.count; j++) {
                string_append(&command, "./build/");
                string_append(&command, job.objects.elements[j]);
                string_append(&command, " ");
            }
        }
        if (job.kind == TargetKind_Executable) {
            string_append(&command, "ccache cc -o ./build/");
            string_append(&command, job.name);
            string_append(&command, " ");

            string_append(&command, "-L./build ");

            for (usize j = 0; j < job.libraries.count; j++) {
                string_append(&command, "-l");
                string_append(&command, job.libraries.elements[j]);
                string_append(&command, " ");
            }

            for (usize j = 0; j < job.objects.count; j++) {
                string_append(&command, "./build/");
                string_append(&command, job.objects.elements[j]);
                string_append(&command, " ");
            }
        }
        vec_append(&command, '\0');
        if (system(command.elements) != 0)
            exit(1);
    }
}

static inline c_string generate_compile_commands(Schedule* schedule)
{
    String compile_commands = { 0 };
    string_append(&compile_commands, "[\n");

    for (usize i = 0; i < schedule->compile.count; i++) {
        CompileJob job = schedule->compile.elements[i];

        String command = { 0 };
        string_append(&command, "ccache cc -c -o ./build/");
        string_append(&command, job.name);
        string_append(&command, " ");

        for (usize j = 0; j < job.include_directories.count; j++) {
            c_string dir = job.include_directories.elements[j];
            string_append(&command, "-I");
            string_append(&command, dir);
            string_append(&command, " ");
        }
        string_append(&command, job.source);

        string_append(&compile_commands, "  {\n");
        string_append(&compile_commands, "    \"directory\": \"");
        string_append(&compile_commands, getenv("PWD"));
        string_append(&compile_commands, "\",\n");

        string_append(&compile_commands, "    \"command\": \"");
        string_append(&compile_commands, command.elements);
        string_append(&compile_commands, "\",\n");

        string_append(&compile_commands, "    \"file\": \"");
        string_append(&compile_commands, job.source);
        string_append(&compile_commands, "\",\n");

        string_append(&compile_commands, "    \"output\": \"./build/");
        string_append(&compile_commands, job.name);
        string_append(&compile_commands, "\"\n");
        string_append(&compile_commands, "  }");
        if ((i + 1) < schedule->compile.count)
            string_append(&compile_commands, ",");
        string_append(&compile_commands, "\n");
    }
    string_append(&compile_commands, "]\n");
    vec_append(&compile_commands, '\0');

    return compile_commands.elements;
}

static inline void build(Target* target)
{
    Schedule schedule = { 0 };
    schedule_build(target, &schedule);
    system("rm -rf build");

    compile_objects(&schedule);
    link_targets(&schedule);

    printf("\r\033[KGenerating compile_commands.json\n");
    FILE* f = fopen("./build/compile_commands.json", "w+");
    (void)fprintf(f, "%s", generate_compile_commands(&schedule));
    (void)fclose(f);
}
