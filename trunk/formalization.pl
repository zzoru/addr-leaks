/******************************************************************************
  The operational semantics of angels.
  ====================================
  - We let a store list be a list of pairs (V', Val'), such that for any two
  pairs (V1, _) and (V2, _), we have that V1 \= V2.
  - The values that are stored in the store list can either be #, meaning
  any clean, harmless value, or an address of a variable.
 ******************************************************************************/

/*
   update(T, V, Val, TT) is true whenever T is a store list and TT is the same
   list as T, except that it has the pair (V, Val).
 */
update([], V, Val, [(V, Val)]).
update([(V, _)|T], V, Val, [(V, Val)|T]).
update([(U, UVal)|T], V, Val, [(U, UVal)|TT]) :-
  \+(U = V),
  update(T, V, Val, TT).

/*
   get(T, V, Val) is true whenever the store list T contains the pair (V, Val).
 */
get([(V, Val)|_], V, Val).
get([(U, _)|T], V, Val) :- \+(U = V), get(T, V, Val).

/*
   meet(T, S, State) is the meet of concrete states, using the binary meet
   operator that and address plus anything is an address. We have that T is a
   list of variables, and S is a store list. If any of the variables in T is
   mapped to an address in S, then the result of the meet is this address.
   If there are several variables mapped to addresses, we take the first one.
   Otherwise, the result of the meet is the clean value #.
 */
meet([], _, #).
meet([V|_], S, Val) :- get(S, V, Val), \+(Val = #).
meet([V|T], S, State) :- get(S, V, #), meet(T, S, State).

/*
   shadow_meet(T, S, ShadowState) is the meet of the variables of the list T
   over the shadow environment S.
 */
shadow_meet([], _, clean).
shadow_meet([V|_], S, tainted) :- get(S, shadow(V), tainted).
shadow_meet([V|T], S, ShadowState) :-
  get(S, shadow(V), clean), shadow_meet(T, S, ShadowState).

/*
   The semantics of instructions. The predicate proc_inst(I, S, S') is true
   whenever the result of processing instruction I in the store S is the new
   store S'.
   We distinguish between two kinds of instructions: ordinary and instrumented.
   The first category includes the instructions present in the original
   program, whereas the second represents the instructions inserted by the
   instrumentation pass. The instrumented instructions have the prefix
   'shadow_'.
   We show the semantics of the ordinary instructions first:
 */
proc_inst(getad(V1, V2), S, SS) :- update(S, V1, V2, SS).

proc_inst(print(V), S, S) :- get(S, V, Val), write(Val).

proc_inst(simop(V1, L0), S, SS) :- meet(L0, S, Val), update(S, V1, Val, SS).

proc_inst(stmem(V0, V1), S, SS) :-
  get(S, V0, X), get(S, V1, Val), update(S, X, Val, SS).

proc_inst(ldmem(V1, V0), S, SS) :-
  get(S, V0, X), get(S, X, Val), update(S, V1, Val, SS).

/* The semantics of the instrumented instructions: */
proc_inst(shadow_getad(V1, V2), S, SSS) :- /* v1 = &v2 */
  update(S, V1, V2, SS), update(SS, shadow(V1), tainted, SSS).

proc_inst(shadow_print(V), S, S) :-
  get(S, shadow(V), ShadowVal), get(S, V, Val),
  write(ShadowVal), write(': '), write(Val), write('\n').

proc_inst(shadow_simop(V1, L0), S, SSS) :- /* x1 = f(x0, ..., xn) */
  shadow_meet(L0, S, ShadowVal), update(S, shadow(V1), ShadowVal, SS),
  meet(L0, SS, Val), update(SS, V1, Val, SSS).

proc_inst(shadow_stmem(V0, V1), S, SSS) :- /* *v0 = v1 */
  get(S, V0, X), get(S, V1, Val), update(S, X, Val, SS),
  get(SS, shadow(V1), ShadowVal), update(SS, shadow(X), ShadowVal, SSS).

proc_inst(shadow_ldmem(V1, V0), S, SSS) :- /* v1 = *v0 */
  get(S, V0, X), get(S, X, Val), update(S, V1, Val, SS),
  get(SS, shadow(X), ShadowVal), update(SS, shadow(V1), ShadowVal, SSS).

/*
   The predicate proc_prog(T, S, S') is true if the result of processing all
   the instructions in T in the initial store S gives back the final store S'.
 */
proc_prog([], S, S).
proc_prog([I|T], S, SSS) :- proc_inst(I, S, SS), proc_prog(T, SS, SSS).

/*
   This is the execution driver. The predicate run(N, S) is true whenever
   N designates a program P, and S is the result of running P on the
   inital empty store.
   Example: run(9, S).
 */
run(N, S) :- program(N, P), proc_prog(P, [], S).

/******************************************************************************
  The full instrumentation.
  =========================
  The goal of this transformation is to instrument the whole program. To
  accomplish this objective, we replace each ordinary instruction by the
  equivalent instrumented instruction.
 ******************************************************************************/

/*
   full_inst_prog(Po, Pi) is true if Po is a program with only ordinary
   instructions, and Pi is the equivalent instrumented program.
 */
full_inst_prog([], []).
full_inst_prog([I|T], [II|TT]) :- apply_full_inst(I, II), full_inst_prog(T, TT).

/*
   apply_full_inst(Io, Ii) is true if Io is an ordinary instruction, and Ii is
   the corresponding instrumented instruction.
 */
apply_full_inst(getad(V1, V2), shadow_getad(V1, V2)).
apply_full_inst(print(V), shadow_print(V)).
apply_full_inst(simop(V1, L0), shadow_simop(V1, L0)).
apply_full_inst(stmem(V0, V1), shadow_stmem(V0, V1)).
apply_full_inst(ldmem(V1, L0), shadow_ldmem(V1, L0)).

/*
   This is the test driver. The predicate run_inst(N, S) is true whenever
   N designates a program P with only ordinary instructions, and S is the store
   that results from running the instrumented version of P on an initially empty
   store.
   ---
   Example: run_inst(9, S).
 */
run_inst(N, S) :- program(N, P), full_inst_prog(P, PI), proc_prog(PI, [], S).

/******************************************************************************
  Points-to analysis.
  ===================
  The goal of this analysis is to find a map of variables to points-to sets,
  given an input program.
  - The map of points-to facts is a list of pairs 'pointsTo(V, P)', where P is
  a list of locations that can be pointed by V, and each V is a unique key.
 ******************************************************************************/

/*
   get_pt(P, V, Pt) is true if the points-to set of V, in the map P, is Pt.
 */
get_pt(P, V, Pt) :- member(pointsTo(V, Pt), P).
get_pt(P, V, []) :- \+(member(pointsTo(V, _), P)).

/*
   union(S1, S2, S) is true whenever S has all the elements of lists S1 and S2,
   and only these elements.
 */
union([], S, S).
union([H|T], S, SS) :- member(H, S), union(T, S, SS).
union([H|T], S, SS) :- \+(member(H, S)), union(T, [H|S], SS).

/*
   upd_pt(T, V, P, TT) is true whenever TT is the same points-to list as T,
   except that it contains the pair 'pointsTo(V, P)'.
 */
upd_pt([], V, P, [pointsTo(V, P)]).
upd_pt([pointsTo(V, _)|T], V, P, [pointsTo(V, P)|T]).
upd_pt([pointsTo(U, Pu)|T], V, P, [pointsTo(U, Pu)|TT]) :-
  V \= U, upd_pt(T, V, P, TT).

/*
   eq_set(S1, S2) is true if S1 contains the same elements as S2, in any order.
 */
eq_set([], []).
eq_set([H|T], S) :- select(H, S, SS), eq_set(T, SS).

/*
   eq_pt(P1, P2) is true if P1 and P2 are points-to lists with the same keys,
   and for each pair 'pointsTo(V, L1)' in P1, and 'pointsTo(V, L2)' in P2,
   we have that eq_set(L1, L2) is true.
 */
eq_pt([], []).
eq_pt([pointsTo(V, P1)|P], PP) :-
  select(pointsTo(V, P2), PP, PPP),
  eq_set(P1, P2),
  eq_pt(P, PPP).

/*
   find_pt(I, P, PP) is true whenever the result of analyzing instruction I,
   given the points-to facts P produces back the new set of points-to facts PP.
 */
find_pt(getad(V1, V2), P, PP) :-   /* V1 = &V2 */
  get_pt(P, V1, P1), union(P1, [V2], P12), upd_pt(P, V1, P12, PP).
find_pt(simop(V1, [V2]), P, PP) :- /* V1 = V2  */
  get_pt(P, V2, P2), get_pt(P, V1, P1),
  union(P1, P2, P12), upd_pt(P, V1, P12, PP).
find_pt(simop(_, L), P, P) :- length(L, X), X =\= 1.
find_pt(stmem(V0, V1), P, PP) :-   /* *v0 = v1 */
  get_pt(P, V0, P0),
  findall(simop(X, [V1]), member(X, P0), Pg),
  solve_pt(Pg, P, PP).
find_pt(ldmem(V1, V0), P, PP) :-   /* v1 = *v0 */
  get_pt(P, V0, P0),
  findall(simop(V1, [X]), member(X, P0), Pg),
  solve_pt(Pg, P, PP).
find_pt(print(_), P, P).

/*
   solve_pt(Pg, P1, P2) is true whenever P2 is the points-to facts that results
   from analyzing the program Pg with the points-to facts P1.
 */
solve_pt([], Pt, Pt).
solve_pt([I|Pg], P1, P3) :-
  find_pt(I, P1, P2), eq_pt(P1, P2), solve_pt(Pg, P1, P3).
solve_pt([I|Pg], P1, P3) :-
  find_pt(I, P1, P2), \+(eq_pt(P1, P2)),
  append(Pg, [I], Pgg), solve_pt(Pgg, P2, P3).

/*
   This is the test driver. The predicate test_pt(N, Pt) is true whenever
   N designates a program P, and Pt is the points-to facts that results from
   performing points-to analysis on P.
   Example: test_pt(11, Pt).
 */
test_pt(N, Pt) :- program(N, Pg), solve_pt(Pg, [], Pt).

/******************************************************************************
  The address leak analysis.
  ==========================
  The goal of this analysis is to determine if it is possible to print a
  value that either is an address, or depends on an address. This possibility
  constitutes a security vulnerability.
  - The main data-structure that we use is a graph G = (V, E), where V are the
  variables in the program, and E contains an edge V1 -> V2 whenever there is
  a data dependence from V1 to V2.
 ******************************************************************************/

/*
   The algorithm that builds the dependence graph.
   ----
   The predicate build_edge(I, Pt, E) is true whenever E is the set of edges
   that result from analyzing the instruction I given the points-to facts Pt.
 */
build_edge(getad(V1, V2), _, [edge(value(V1), addr(V2))]). /* v1 = &v2 */

build_edge(print(V), _, [edge(sink, value(V))]). /* print(x) */

build_edge(simop(V1, LO), _, E) :- /* x1 = f(x0, ..., xn) */
  findall(edge(value(V1), value(XN)), member(XN, LO), E).

build_edge(stmem(V0, V1), PT, E) :- /* *v0 = v1 */
  member(pointsTo(V0, LP), PT),
  findall(edge(value(V), value(V1)), member(V, LP), E).

build_edge(ldmem(V1, V0), PT, E) :- /* v1 = *v0 */
  member(pointsTo(V0, LP), PT),
  findall(edge(value(V1), value(V)), member(V, LP), E).

/*
   build_graph(Pg, Pt, G) is true whenever G is the graph that results from
   analyzing the program Pg with the points-to facts Pt.
 */
build_graph([], _, []).

build_graph([I|T], PT, GG) :-
  build_edge(I, PT, E),
  build_graph(T, PT, G),
  append(G, E, GG).

/*
   The algorithm that traverses the graph looking for address leaks.
   ----
   dfs(V, E, Bug) is true whenever it is possible to reach a sink node from
   the vertice V along the set of edges E. This path, from V to the sink,
   is recorded as the list Bug.
 */
dfs(sink, E, Bug) :-
  member(edge(sink, value(V)), E), dfs(V, E, Bug).
dfs(V, E, [edge(value(V), addr(A))]) :-
  member(edge(value(V), addr(A)), E).
dfs(V, E, [edge(value(V), value(VN))|Bug]) :-
  member(edge(value(V), value(VN)), E), dfs(VN, E, Bug).

/*
   find_leak(Pg, Bug) is true whenever there is a data dependence path between
   some variable V from Pg, that reads an address, and the sink instruction,
   which in our case is the print function.
 */
find_leak(Pg, Bug) :-
  solve_pt(Pg, [], Pt),
  build_graph(Pg, Pt, E),
  dfs(sink, E, Bug).

/*
   The driver that solves the static address leak detection problem.
   solveAddLeak(N, Bug) is true whenever N designates a program P, and Bug
   is a path from a variable V that reads an address in P to a sink function.
   ----
   Example: solveAddLeak(9, Bug).
 */
solveAddLeak(N, Bug) :- program(N, P), find_leak(P, Bug).


/******************************************************************************
  Combining the static and the dynamic analyses.
 ******************************************************************************/
/*
   extract_vars_aux(BuggyPath, Vars) is true if Vars is the set of all the
   variables in the list of edges BuggyPath.
   ---
   Example:
   ?- extract_vars([[edge(value(c), value(a)), edge(value(a), value(b))]],
         [], Vars).
   Vars = [b, a, c]
 */
extract_vars_aux([], []).
extract_vars_aux([edge(value(V1), value(V2))|T], [V1,V2|VarsNew]) :-
  extract_vars_aux(T, VarsNew).
extract_vars_aux([edge(value(V1), addr(V2))|T], [V1,V2|VarsNew]) :-
  extract_vars_aux(T, VarsNew).

/*
   extract_vars(BuggyPaths, VarsAcc, FinalVars) is true if FinalVars is the set
   of all the variables that appear in the set of edge paths in BuggyPaths.
   VarsAcc is just an accumulator to speedup the resolution of the predicate.
   ---
   Example:
   ?- extract_vars([[edge(value(c), value(a))],
         [edge(value(a), value(b))]], [], Vars).
   Vars = [b, a, c] ;
 */
extract_vars([], Vars, Vars).
extract_vars([Bug|Bugs], Vars, VarsNew) :-
  extract_vars_aux(Bug, VarsAux),
  union(VarsAux, Vars, VarsUnion),
  extract_vars(Bugs, VarsUnion, VarsNew).

/*
   part_inst_prog(Po, Vars, Pi) is true if Po is a program with only ordinary
   instructions, and Pi is the equivalent partially instrumented program, given
   the set Vars of tainted variables.
 */
part_inst_prog([], _, []).
part_inst_prog([I|T], Vars, [II|TT]) :-
  apply_part_inst(I, Vars, II), part_inst_prog(T, Vars, TT).

/*
   apply_part_inst(I, Vars, Is) is true if Is is the instrumented instruction
   that we obtain from I via partial instrumentation. The partial
   instrumentation only changes instructions whose left-hand side is in the
   set Vars of variable names.
 */
apply_part_inst(getad(V1, V2), Vars, shadow_getad(V1, V2)) :- member(V1, Vars).
apply_part_inst(getad(V1, V2), Vars, getad(V1, V2)) :- \+(member(V1, Vars)).
apply_part_inst(print(V), Vars, shadow_print(V)) :- member(V, Vars).
apply_part_inst(print(V), Vars, print(V)) :- \+(member(V, Vars)).
apply_part_inst(simop(V1, L0), Vars, shadow_simop(V1, L0)) :- member(V1, Vars).
apply_part_inst(simop(V1, L0), Vars, simop(V1, L0)) :- \+(member(V1, Vars)).
apply_part_inst(stmem(V0, V1), _, shadow_stmem(V0, V1)).
apply_part_inst(ldmem(V1, V0), Vars, shadow_ldmem(V1, V0)) :- member(V1, Vars).
apply_part_inst(ldmem(V1, V0), Vars, ldmem(V1, V0)) :- \+(member(V1, Vars)).

/*
   apply_partial_instrumentation(Pg, Pi) is true whenever Pi is a partially
   instrumented program that we get by instrumenting only the variables that
   are in buggy paths of Pg. We get the buggy paths via the static address leak
   detection.
 */
apply_partial_instrumentation(Pg, Pi) :-
  findall(Bug, find_leak(Pg, Bug), Bugs),
  extract_vars(Bugs, [], Vars),
  write(Vars),
  part_inst_prog(Pg, Vars, Pi).

/*
   This is the test driver. The predicate test_part_inst(N, Pi) is true whenever
   N designates a program P, and Pi is the result of partially instrumenting P.
   ---
   Example: test_part_inst(13, S).
 */
test_part_inst(N, Pi) :- program(N, P), apply_partial_instrumentation(P, Pi).

/******************************************************************************
  Examples of programs.
  =====================
  Each example is given by a predicate program(N, L), where N is a unique
  identifier, and L is the list of instructions that represent the program.
  In this way, the query program(N, P) will always return a program given a
  valid identifier N.
 ******************************************************************************/
program(1, [
    getad(p0, m0),       /* p0 = alloc_0; */
    simop(v0, []),       /* v0 = 0;  */
    simop(v1, [p0]),     /* v1 = (int)p0;  */
    simop(v2, [v0, v1]), /* v2 = v1 + v0;  */
    print(v2)            /* print(v2) */
]).

program(2, [
    simop(v0, []),       /* v0 = 0;  */
    getad(v1, v0),       /* v1 = & v0;  */
    simop(v2, [v0, v1]), /* v2 = v0 + v1;  */
    print(v2)            /* print(v2) */
]).

program(3, [
    simop(v0, []),       /* v0 = 0;  */
    simop(v1, [v0]),     /* v1 = v0 + 1;  */
    simop(v2, [v0, v1]), /* v2 = v0 + v1;  */
    print(v2)            /* print(v2) */
]).

program(4, [             /* This program is not executable */
    getad(v0, m1),       /* v0 = alloc_1; */
    ldmem(v1, m1),       /* v1 = *alloc_1;  */
    print(v1),           /* print(v1);  */
    getad(v2, m2),       /* v2 = alloc_2;  */
    stmem(v0, v2)        /* *v0 = v2;  */
]).

program(5, [
    getad(v0, m1),
    getad(t0, m2),
    stmem(v0, t0),
    ldmem(v1, v0),
    ldmem(t1, v1),
    print(t1),
    getad(v2, m3),
    ldmem(t2, v0),
    stmem(t2, v2)
]).

program(6, [
    simop(n1, [n0]),
    getad(v0, m1),
    simop(n0, [v0]),
    simop(v1, [n1]),
    stmem(v1, v4),
    ldmem(v5, v1),
    print(v5),
    simop(n0, [v2]),
    simop(v3, [n1]),
    print(v3)
]).

program(7, [
    simop(v0, []),       /* v0 = 0 */
    getad(v1, v0),       /* v1 = &v0 */
    simop(v2, []),       /* v2 = 0 */
    stmem(v1, v2),       /* *v1 = v2  */
    print(v0)
]).

program(8, [
    simop(v0, []),       /* v0 = 0 */
    getad(v1, v0),       /* v1 = &v0 */
    getad(v2, v0),       /* v2 = &v0 */
    stmem(v1, v2),       /* *v1 = v2  */
    print(v0)
]).

program(9, [
    getad(v1, v0),       /* v1 = &v0 */
    getad(v2, v0),       /* v2 = &v0 */
    stmem(v1, v2),       /* *v1 = v2  */
    ldmem(v3, v2),       /* v3 = *v2  */
    print(v3)
]).

program(10, [
    getad(b, c),       /* b, &c */
    ldmem(d, b),       /* d = *b  */
    simop(a, [d]),     /* a = d */
    stmem(b, a),       /* *b = a  */
    getad(d, m)        /* d = &m */
]).

program(11, [
    getad(b, c),       /* b = &c */
    ldmem(d, b),       /* d = *b  */
    simop(a, [d]),     /* a = d */
    stmem(b, a),       /* *b = a  */
    getad(d, e),       /* d = &e */
    simop(e, [b]),     /* e = b */
    stmem(e, e)        /* *e = e */
]).

program(12, [ /* This program shows that our analysis catches all the buggy
                 paths. */
    getad(a, b),       /* a = &b */
    getad(c, d),       /* c = &d */
    print(a),
    print(c)
]).

program(13, [
    getad(a, m),
    simop(b, [a, c]),
    simop(d, [a, g]),
    print(b),
    simop(e, [g, h]),
    simop(f, [d, e]),
    print(f)
]).
