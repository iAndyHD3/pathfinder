# Summary of the Google C++ Style Guide (for non-Google projects)

## Guiding principles
The guide says its rules exist to serve a few goals: optimize for the reader rather than the writer, be consistent, avoid surprising or dangerous constructs, avoid tricky constructs average programmers can't maintain, be mindful of scale, and concede to optimization when necessary. Each rule should "pull its weight." It targets C++20 and bans non-standard extensions.

## Rules that apply to any project

**Headers**
- Headers must be self-contained and use include guards named `PROJECT_PATH_FILE_H_`.
- Include what you use, and don't rely on transitive includes.
- Avoid forward declarations, and never forward-declare things you don't own.
- Define functions in headers only if they're short (about 10 lines or fewer).
- Include order: related header, C system headers, C++ standard headers, other libraries, your project. Each group is separated by a blank line and sorted alphabetically.

**Scoping**
- Put code in namespaces, and avoid `using namespace` and inline namespaces.
- Give `.cc`-only definitions internal linkage (unnamed namespace or `static`), but not in headers.
- Prefer namespaced free functions over classes that only hold static members.
- Declare locals in the narrowest scope and initialize them at declaration.
- Static and global objects must be trivially destructible. Dynamic initialization of non-local statics is discouraged.
- Namespace/class-scope `thread_local` variables need `constinit`.

**Classes**
- Don't call virtual functions in constructors, and avoid initialization that can fail silently.
- Mark single-argument constructors and conversion operators `explicit`.
- Make copyability and movability explicit in the public interface.
- Use `struct` only for passive data. Prefer structs with named fields over `pair` or `tuple`.
- Prefer composition over inheritance. Inheritance should be public, use `override` or `final`, and avoid multiple implementation inheritance.
- Overload operators only when the meaning is obvious. No user-defined literals, and no overloading `&&`, `||`, `,` or unary `&`.
- Data members are private, and declarations go in a set order (public first).

**Functions**
- Prefer return values over output parameters, and put input parameters before output ones.
- Prefer short, focused functions. Consider splitting anything over about 40 lines.
- Overload only when callers don't need to know which overload is chosen.
- Don't use default arguments on virtual functions.
- Use trailing return types only where needed, such as lambdas.

**Language features**
- Single owners and `std::unique_ptr` are preferred. Use `shared_ptr` only with good reason, ideally to const data.
- Rvalue references are allowed only in specific cases: move operations, `&&`-qualified methods, perfect forwarding, and overload pairs.
- Don't use exceptions. The guide frames this as a practical decision based on Google's existing code, not a philosophical one.
- Avoid RTTI outside tests.
- Use C++-style casts or brace initialization, not C-style casts.
- Use `const` heavily in APIs and `constexpr` for true constants.
- Use `int` for ordinary integers, fixed-width types like `int64_t` when you need a size guarantee, and don't use unsigned types just to mean "non-negative." Skip `long double`.
- Use `nullptr`, `sizeof(varname)`, and prefix `++i`.
- Avoid macros. If you must use one, give it a project prefix and `#undef` it after use.
- Use `auto` only when it makes code clearer or safer.
- Use class template argument deduction (CTAD) only with templates that opted in, and prefer explicit lambda captures when the lambda escapes its scope.
- Avoid complicated template metaprogramming and use concepts sparingly. Don't use C++20 modules.
- `switch` needs a `default` (unless over an enum) and `[[fallthrough]]` where cases fall through.
- Disallowed standard headers: `<ratio>`, `<cfenv>`/`<fenv.h>`, `<filesystem>`.
- Use inclusive language.

## Naming
| Thing | Convention |
|---|---|
| Files | lowercase, `_` or `-` (follow your project); `.cc` / `.h` |
| Types, concepts | `PascalCase` |
| Variables, params | `snake_case`; class data members get a trailing `_`, struct members don't |
| Constants | `kPascalCase` |
| Enumerators | `kPascalCase` (not `ALL_CAPS`) |
| Functions | `PascalCase`; trivial accessors/mutators may be `snake_case` |
| Namespaces | `snake_case` |
| Macros | `ALL_CAPS` with project prefix |

## Comments
- Use `//` or `/* */` consistently.
- Describe what and why, not the obvious.
- Non-obvious classes and most functions need comments: inputs, outputs, pointer nullability, and lifetime.
- `TODO` comments need an identifier (a bug ID or name).

## Formatting
- 80-column limit, with listed exceptions such as URLs, includes, and header guards.
- Spaces only, 2-space indent, UTF-8 for non-ASCII.
- Always use braces on control statements, except the discouraged single-line cases.
- Pointer style is `char* c`, and `public:`/`private:` are indented one space.
- Namespace contents are not indented.

## What's tied to Google's environment
These parts of the guide depend on Google-specific tooling or processes, so you'd need to decide how to handle them:
- **Abseil** is referenced throughout (`absl::StrCat`, `absl::Status`, `absl::down_cast`, `absl::implicit_cast`, `ABSL_FLAG`). The guide ranks Abseil first among preferred libraries, followed by the standard library.
- **"Ask your project leads" waivers** for things like macros, implicit conversions, and coroutine libraries.
- **Abseil Tips of the Week** links and **Google Test**/**Protocol Buffers** mentions.
- **The exceptions ban** is justified by Google's existing code not being exception-safe. The guide admits things "would probably be different" starting from scratch.
- **Third-party library policy** defers to your codebase's own rules.

The guide itself says to defer to your project's existing conventions in places (file naming, existing non-conforming code, third-party library choices). `cpplint.py` is a public tool you can run to check many of the style rules.
