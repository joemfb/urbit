/* j/6/fork.c
**
*/
#include "all.h"


/* functions
*/
  static void
  _lose_set(u3_noun a)
  {
    u3_noun l_a, n_a, r_a;

    if ( (c3n == u3r_trel(a, &n_a, &l_a, &r_a)) ) {
      return;
    }

    u3z(n_a);
    _lose_set(l_a);
    _lose_set(r_a);
  }

  static u3_noun
  _fork_cell(u3_noun lez)
  {
    u3_noun lit = u3qdi_tap(lez, u3_nul);
    u3_noun l = u3_nul;
    u3_noun r = u3_nul;

    while ( u3_nul != lit ) {
      u3_noun i_lit = u3h(lit);

      if ( c3__cell != u3h(i_lit) ) {
        _lose_set(l);
        _lose_set(r);
        return u3nc(c3__fork, lez);
      }

      u3_noun t_i_lit = u3t(i_lit);

      l = u3qdi_put(l, u3k(u3h(t_i_lit)));
      r = u3qdi_put(r, u3k(u3t(t_i_lit)));

      lit = u3t(lit);
    }

    if ( 1 == u3qdi_wyt(l) ) {
      u3z(lez);
      return u3nt(c3__cell, u3h(l), _fork_cell(r));
    }

    if ( 1 == u3qdi_wyt(r) ) {
      u3z(lez);
      return u3nt(c3__cell, _fork_cell(l), u3h(r));
    }

    _lose_set(l);
    _lose_set(r);
    return u3nc(c3__fork, lez);
  }

  u3_noun
  u3qf_forq(u3_noun hoz,
            u3_noun bur)
  {
    if ( c3y == u3r_sing(hoz, bur) ) {
      return u3k(hoz);
    }
    else if ( c3__void == bur ) {
      return u3k(hoz);
    }
    else if ( c3__void == hoz ) {
      return u3k(bur);
    }
    else return u3kf_fork(u3nt(u3k(hoz), u3k(bur), u3_nul));
  }

  u3_noun
  u3qf_fork(u3_noun yed)
  {
    u3_noun lez = u3_nul;

    while ( u3_nul != yed ) {
      u3_noun i_yed = u3h(yed);

      if ( c3__void != i_yed ) {
        if ( (c3y == u3du(i_yed)) && (c3__fork == u3h(i_yed)) ) {
          lez = u3kdi_uni(lez, u3k(u3t(i_yed)));
        }
        else {
          lez = u3kdi_put(lez, u3k(i_yed));
        }
      }

      yed = u3t(yed);
    }

    if ( u3_nul == lez ) {
      return c3__void;
    }
    else if ( (u3_nul == u3h(u3t(lez))) && (u3_nul == u3t(u3t(lez))) ) {
      u3_noun ret = u3k(u3h(lez));

      u3z(lez);
      return ret;
    }
    else {
      return _fork_cell(lez);
    }
  }

  u3_noun
  u3wf_fork(u3_noun cor)
  {
    u3_noun yed;

    if ( c3n == u3r_mean(cor, u3x_sam, &yed, 0) ) {
      return u3m_bail(c3__fail);
    } else {
      return u3qf_fork(yed);
    }
  }

  u3_noun
  u3kf_fork(u3_noun yed)
  {
    u3_noun ret = u3qf_fork(yed);

    u3z(yed);
    return ret;
  }
