## ▶️ Reproducibility Guide

All commands below are executed from the repository root.

### 1. Construct anchor pairs from the symbolic trace

```bash
gcc -O2 -std=c11 -o anchors src/pipeline/make_exact_anchor_pairs_win64.c
./anchors
```

This step extracts the canonical anchor pairs from the symbolic trace and writes:

```text
data/exact_anchor_pairs.csv
```

---

### 2. Generate macro-profile and debt-transition data

```bash
gcc -O2 -std=c11 -o gen src/data_generation/q_D_macro_profiles_stream_occ_win64.c
./gen
```

This step generates the observed macro profiles and debt-transition data used for the compressed symbolic system.

---

### 3. Compress the observed transition structure to the 9-state system

```bash
gcc -O2 -std=c11 -o compress src/pipeline/compress_edges_to_9state.c
./compress
```

This step constructs the compressed symbolic transition system used throughout the proof.

---

### 4. Construct the finite local model and propagate constraints

```bash
gcc -O2 -std=c11 -o propagate src/pipeline/construct_and_propagate.c
./propagate
```

This is the core reconstruction step. It:

* constructs the admissible context set $\Lambda_{\mathrm{adm}}$
* constructs the compatibility relation $\Psi$
* enforces debt rigidity
* performs local constraint propagation
* extracts the induced critical-core structure

---

### 5. Construct the two-mode automaton

```bash
gcc -O2 -std=c11 -o two_mode src/automaton/two_mode_automaton.c
./two_mode
```

This step computes the two global modes of valid assignments, referred to in the paper as **Mode A** and **Mode B**.

---

### 6. Independently validate the two-mode decomposition

```bash
gcc -O2 -std=c11 -o branch src/automaton/branch_comparison_solver.c
./branch
```

This step provides an independent solver-based verification of the two-mode structure.

---

### 7. Verify the critical core exhaustively

```bash
g++ -O2 -std=c++17 -o checkall src/verification/checkall.cpp
./checkall
```

Expected final output:

```text
VERIFIED: no simultaneous obstruction on the critical core.
```

This is the final finite verification step used in the proof.

---

## 📄 Role of the Pipeline in the Proof

The formal computer-assisted part of the proof depends on the following chain:

1. finite symbolic compression of the observed dynamics,
2. reconstruction of the admissible context graph,
3. derivation of the two-mode structure,
4. independent validation of that two-mode decomposition,
5. exhaustive verification that no obstruction exists on the critical core.

In particular, the final proof certificate is carried by the programs

* `src/pipeline/construct_and_propagate.c`
* `src/automaton/two_mode_automaton.c`
* `src/automaton/branch_comparison_solver.c`
* `src/verification/checkall.cpp`

These are the programs that should be regarded as the **formal proof pipeline** for the finite verification component of the paper.
