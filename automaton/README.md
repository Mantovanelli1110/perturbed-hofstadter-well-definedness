# Two-Mode Automaton Construction

This directory contains the program

    two_mode_automaton.c

which constructs the finite automaton underlying the proof.

## Purpose

The program:

- reconstructs the admissible local contexts
- builds the local transition graph Ψ
- performs constraint propagation
- computes all consistent assignments
- extracts the support sets
- identifies the two global modes (Mode A and Mode B)

## Relation to the Paper

This corresponds to:

- Sections 6–8 (rigidity and two-mode decomposition)
- the construction of support sets
- the finite-state model of the recursion

## Output

The program prints:

- domains after propagation
- support sets for both modes
- intersection and differences between modes

## Compilation

```bash
gcc -O2 two_mode_automaton.c -o automaton
./automaton
