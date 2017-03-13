/* j/6/cell.c
**
*/
#include "all.h"


/* functions
*/
  u3_noun
  u3qf_cell(u3_noun hed,
            u3_noun tal)
  {
    if ( (c3__void == hed) || (c3__void == tal) ) {
      return c3__void;
    }

    if ( ( (c3y == u3du(hed)) && (c3__fork == u3h(hed)) ) ||
         ( (c3y == u3du(tal)) && (c3__fork == u3h(tal)) ) ) {
      u3_noun lit = u3_nul;

      if ( (c3y != u3du(tal)) || (c3__fork != u3h(tal)) ) {
        u3_noun hed_l = u3qdi_tap(u3t(hed), u3_nul);

        while ( hed_l != u3_nul ) {
          lit = u3nc(u3nt(c3__cell, u3k(u3h(hed_l)), u3k(tal)), lit);
          hed_l = u3t(hed_l);
        }
      } else if ( (c3y != u3du(hed)) || (c3__fork != u3h(hed)) ) {
        u3_noun tal_l = u3qdi_tap(u3t(tal), u3_nul);

        while ( tal_l != u3_nul ) {
          lit = u3nc(u3nt(c3__cell, u3k(hed), u3k(u3h(tal_l))), lit);
          tal_l = u3t(tal_l);
        }
      } else {
        u3_noun hed_l = u3qdi_tap(u3t(hed), u3_nul);
        u3_noun ttal_l = u3qdi_tap(u3t(tal), u3_nul);

        while ( hed_l != u3_nul ) {
          u3_noun tal_l = ttal_l;

          while ( tal_l != u3_nul ) {
            lit = u3nc(u3nt(c3__cell, u3k(u3h(hed_l)), u3k(u3h(tal_l))), lit);
            tal_l = u3t(tal_l);
          }

          hed_l = u3t(hed_l);
        }
      }

      return u3qf_fork(lit);
    }

    return u3nt(c3__cell, u3k(hed), u3k(tal));
  }
  u3_noun
  u3wf_cell(u3_noun cor)
  {
    u3_noun hed, tal;

    if ( c3n == u3r_mean(cor, u3x_sam_2, &hed, u3x_sam_3, &tal, 0) ) {
      return u3m_bail(c3__fail);
    } else {
      return u3qf_cell(hed, tal);
    }
  }
