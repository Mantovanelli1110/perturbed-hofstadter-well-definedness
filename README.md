# perturbed-hofstadter-well-definedness
This repository contains the standalone verification code accompanying the paper “A Finite-State Proof of the Well-Definedness of a Perturbed Hofstadter Sequence”.
## Overview

The paper proves that the perturbed Hofstadter-type recursion

\[
Q(n) = Q(n - Q(n-1)) + Q(n - Q(n-2)) + (-1)^n
\]

with initial values \(Q(1) = Q(2) = 1\) is well-defined for all \(n \ge 1\).

The proof reduces the infinite recursion to a **finite constraint system**, and ultimately to a **finite verification problem on a critical core of four contexts**.

This repository provides a **standalone verification program** for this final step.

---

## What the Code Verifies

The program `checkall_en.cpp` performs the following:

- Enumerates all **15 nonempty subsets** of the critical core  
  \[
  \Lambda_{\mathrm{crit}} = \{\lambda_0, \lambda_2, \lambda_{14}, \lambda_{24}\}
  \]

- For each subset \(H\), checks whether there exists an assignment  
  \[
  f : H \to S \times \{0,2\}
  \]
  satisfying all compatibility constraints

- Uses the explicitly computed:
  - support sets \(\Sigma_A, \Sigma_B\)
  - induced compatibility relation \(\Psi\)

- Confirms that **every subset admits a valid assignment**

---

## Result

The program verifies that:

> No subset of the critical core is an obstruction.

This establishes the final step of the proof:
- No obstruction exists in the finite system  
- Therefore, the recursion is globally well-defined

---

## Compilation & Usage

Compile with:

```bash
g++ -O2 -std=c++17 -o checkall checkall_en.cpp
