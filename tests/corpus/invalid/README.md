# Negative fixtures

Each file here is **invalid on purpose**. A file declares the diagnostic codes
it must provoke with `# EXPECT <CODE>` lines in its header; the self-test in
`tests/check_stardata.py --self-test` asserts that every declared code is
actually reported.

Extra diagnostics beyond the declared ones are tolerated, because one mistake
often cascades. The point of the fixture is that the *named* rule fires.

Add a fixture whenever the specification gains a rule that can be violated.
