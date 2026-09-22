# Contributing

## Workflow

- `main` is always green. Work on a branch (`feat/...`, `fix/...`, `docs/...`, `hw/...`) and
  merge through a pull request, even as a single developer: the PR is where CI and the review
  checklist run.
- Commit messages follow [Conventional Commits](https://www.conventionalcommits.org):
  `feat(icd): add ATTEN register to payload_ctrl`.
- One logical change per commit. Generated files (`libs/regs/include/satlink/regs/`,
  `docs/icd/generated/`, `docs/traceability/`) are committed together with their source.

## Before pushing

```bash
pre-commit run --all-files            # formatting, whitespace, regenerate ICD + trace matrix
cmake --preset host-debug && cmake --build --preset host-debug && ctest --preset host-debug
python3 -m pytest tools
```

## Coding rules

- **C (firmware, libraries):** C11, oriented on MISRA C:2012. No dynamic memory, no recursion,
  explicit casts, fixed-width types, one exit path where it keeps the code clear, `static` for
  file-local symbols, public symbols prefixed `satlink_`. Deviations from MISRA rules are
  justified in a comment and listed in the MISRA report.
- **C++ (Linux, host):** C++20, C++ Core Guidelines. RAII for every resource, no raw
  `new`/`delete`, `constexpr` where possible, interfaces behind abstract classes so they can be
  mocked with GoogleMock.
- **Registers:** only through the generated headers. Never a raw offset or mask in code.
- **Traceability:** add `@implements SRS-XXX-NNN` to the file that implements a requirement and
  `@verifies SRS-XXX-NNN` to the test that verifies it.
