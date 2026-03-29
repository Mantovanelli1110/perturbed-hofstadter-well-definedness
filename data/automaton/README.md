### Automaton data

- `data/automaton/real_edges.txt`  
  Contains the compressed transition relation (37 edges) of the stabilized automaton used in the verification.  
  Each entry has the form `{sid_from, debt_from, sid_to, debt_to}`.

  The JSON version is provided for machine verification; it is equivalent to the C array used in the verifier.
