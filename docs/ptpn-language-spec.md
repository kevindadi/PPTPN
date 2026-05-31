# PTPN Language Specification (v1.0)

## Overview

PTPN (Priority Timed Petri Net) is a domain-specific language for defining
Petri net structures with places, transitions, arcs, and their attributes.

---

## Grammar

### 1. Comments

```
// single-line comment
/* multi-line comment */
```

### 2. Places

```
places <place_def>, <place_def>, ...
```

**Place definition**: `<id>[:<name>][:capacity]`

- `<id>`: Required, alphanumeric (e.g., P0, P1, Place_1)
- `<name>`: Optional, human-readable name
- `<capacity>`: Optional, default=1

**Examples:**
```
places P0, P1, P2
places P0:Start, P1:Running:2
places Entry:1, Buffer:5, Exit:1
```

### 3. Transitions

```
transitions
    <transition_def>
    <transition_def>
```

**Transition definition**: `<id>[:<name>] [<time_range>] [@<attr>, ...]`

- `<id>`: Required, alphanumeric (e.g., T0, T1)
- `<name>`: Optional, human-readable name
- `<time_range>`: Required, format `[min, max]` (both inclusive)
- `<attr>`: Optional attributes

**Attributes:**
| Attribute | Syntax | Example |
|-----------|--------|---------|
| Priority | `@priority=N` or `@N` | `@priority=97`, `@98` |
| Core | `@core=N` | `@core=0`, `@core=-1` |
| Suspendable | `suspendable` | `suspendable` |
| Capacity | `@capacity=N` | `@capacity=1` |

**Examples:**
```
transitions
    T0 [0, 0] @priority=0, core=-1
    T1 [3, 8] @priority=97, core=0, suspendable
    T2 [5, 5] @priority=98, core=0
    T3 [8, 10] @priority=99, core=1
```

### 4. Arcs (Flow Relations)

```
<source> -> <target>
<source> -> <target> : <weight>
```

- `<source>`: Place or Transition ID
- `<target>`: Place or Transition ID
- `<weight>`: Optional, default=1

**Examples:**
```
P0 -> T0
T0 -> P1
P1 -> T2 : 2
```

### 5. Initial Marking

```
@init <place>:<tokens>, <place>:<tokens>, ...
```

**Examples:**
```
@init P0:1, P1:1
@init Entry:1
```

### 6. Capacity (optional)

```
@capacity <place>:<cap>, <place>:<cap>, ...
```

**Examples:**
```
@capacity Buffer:5, Shared:2
```

---

## Complete Example

```ptpn
// ===========================================
// Priority Timed Petri Net
// Simple example
// ===========================================

// === Places ===
places
    P0: Start:1
    P1: Running:1
    P2: Shared:2
    P3: End:1

// === Transitions ===
transitions
    T0: Init   [0, 0]   @priority=0, core=-1
    T1: A      [3, 8]   @priority=97, core=0, suspendable
    T2: B      [5, 5]   @priority=98, core=0
    T3: C      [8, 10]  @priority=99, core=1

// === Arcs ===
P0 -> T0
T0 -> P1
P1 -> T1
T1 -> P2
P2 -> T2
T2 -> P3
P3 -> T3

// === Initial Marking ===
@init P0:1, P1:1

// === Capacity ===
@capacity P2:2
```

---

## Lexical Tokens

| Token Type | Pattern | Example |
|------------|---------|---------|
| IDENTIFIER | `[a-zA-Z_][a-zA-Z0-9_]*` | `P0`, `TaskA`, `Core_1` |
| NUMBER | `[0-9]+` | `42`, `100` |
| LBRACKET | `[` | `[` |
| RBRACKET | `]` | `]` |
| LBRACE | `{` | `{` |
| RBRACE | `}` | `}` |
| COMMA | `,` | `,` |
| COLON | `:` | `:` |
| AT | `@` | `@` |
| ARROW | `->` | `->` |
| DOT | `.` | `.` |

---

## Keywords

```
places, transitions, suspendable
init, capacity
```

---

## Reserved Keywords

```
places, transitions, suspendable, init, capacity
```

---

## Error Handling

1. **Lexical errors**: Invalid characters, unclosed brackets
2. **Syntax errors**: Unexpected tokens, missing required fields
3. **Semantic errors**:
   - Undefined place/transition reference
   - Duplicate place/transition definition
   - Invalid arc (source or target not found)
   - Negative capacity or tokens

---

## File Extension

`.ptpn` for PTPN source files.

---

## Version History

- v1.0 (2026-05-31): Initial specification