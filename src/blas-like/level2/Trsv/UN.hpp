/*
   Copyright (c) 2009-2014, Jack Poulson
   All rights reserved.

   This file is part of Elemental and is under the BSD 2-Clause License, 
   which can be found in the LICENSE file in the root directory, or at 
   http://opensource.org/licenses/BSD-2-Clause
*/

namespace El {
namespace trsv {

template<typename F>
inline void
UN
( UnitOrNonUnit diag, 
  const AbstractDistMatrix<F>& UPre, AbstractDistMatrix<F>& xPre )
{
    DEBUG_ONLY(
        CallStackEntry cse("trsv::UN");
        AssertSameGrids( UPre, xPre );
        if( UPre.Height() != UPre.Width() )
            LogicError("U must be square");
        if( xPre.Width() != 1 && xPre.Height() != 1 )
            LogicError("x must be a vector");
        const Int xLength =
            ( xPre.Width() == 1 ? xPre.Height() : xPre.Width() );
        if( UPre.Width() != xLength )
            LogicError("Nonconformal");
    )
    const Int m = UPre.Height();
    const Int bsize = Blocksize();
    const Int kLast = LastOffset( m, bsize );
    const Grid& g = UPre.Grid();

    DistMatrix<F> U(g), x(g);
    Copy( UPre, U, READ_PROXY );
    Copy( xPre, x, READ_WRITE_PROXY );

    // Matrix views
    DistMatrix<F> U01(g), U11(g), x1(g);

    // Temporary distributions
    DistMatrix<F,STAR,STAR> U11_STAR_STAR(g), x1_STAR_STAR(g);

    if( x.Width() == 1 )
    {
        DistMatrix<F,MR,STAR> x1_MR_STAR(g);
        DistMatrix<F,MC,STAR> z_MC_STAR(g);

        // Views of z[MC,* ], which will store updates to x
        DistMatrix<F,MC,STAR> z0_MC_STAR(g), z1_MC_STAR(g);

        z_MC_STAR.AlignWith( U );
        Zeros( z_MC_STAR, m, 1 );

        for( Int k=kLast; k>=0; k-=bsize )
        {
            const Int nb = Min(bsize,m-k);

            LockedViewRange( U01, U, 0, k, k,    k+nb );
            LockedViewRange( U11, U, k, k, k+nb, k+nb );

            ViewRange( x1, x, k, 0, k+nb, 1 );

            ViewRange( z0_MC_STAR, z_MC_STAR, 0, 0, k,    1 );
            ViewRange( z1_MC_STAR, z_MC_STAR, k, 0, k+nb, 1 );

            if( k+nb != m )
                x1.RowSumScatterUpdate( F(1), z1_MC_STAR );

            x1_STAR_STAR = x1;
            U11_STAR_STAR = U11;
            Trsv
            ( UPPER, NORMAL, diag,
              U11_STAR_STAR.LockedMatrix(), x1_STAR_STAR.Matrix() );
            x1 = x1_STAR_STAR;

            x1_MR_STAR.AlignWith( U01 );
            x1_MR_STAR = x1_STAR_STAR;
            LocalGemv( NORMAL, F(-1), U01, x1_MR_STAR, F(1), z0_MC_STAR );
        }
    }
    else
    {
        DistMatrix<F,STAR,MR> x1_STAR_MR(g);
        DistMatrix<F,MR,  MC> z1_MR_MC(g);
        DistMatrix<F,STAR,MC> z_STAR_MC(g);

        // Views of z[* ,MC]
        DistMatrix<F,STAR,MC> z0_STAR_MC(g), z1_STAR_MC(g);

        z_STAR_MC.AlignWith( U );
        Zeros( z_STAR_MC, 1, m );

        for( Int k=kLast; k>=0; k-=bsize )
        {
            const Int nb = Min(bsize,m-k);

            LockedViewRange( U01, U, 0, k, k,    k+nb );
            LockedViewRange( U11, U, k, k, k+nb, k+nb );

            ViewRange( x1, x, 0, k, 1, k+nb );

            ViewRange( z0_STAR_MC, z_STAR_MC, 0, 0, 1, k    );
            ViewRange( z1_STAR_MC, z_STAR_MC, 0, k, 1, k+nb );

            if( k+nb != m )
            {
                z1_MR_MC.ColSumScatterFrom( z1_STAR_MC );
                Axpy( F(1), z1_MR_MC, x1 );
            }

            x1_STAR_STAR = x1;
            U11_STAR_STAR = U11;
            Trsv
            ( UPPER, NORMAL, diag,
              U11_STAR_STAR.LockedMatrix(), x1_STAR_STAR.Matrix() );
            x1 = x1_STAR_STAR;

            x1_STAR_MR.AlignWith( U01 );
            x1_STAR_MR = x1_STAR_STAR;
            LocalGemv( NORMAL, F(-1), U01, x1_STAR_MR, F(1), z0_STAR_MC );
        }
    }

    Copy( x, xPre, RESTORE_READ_WRITE_PROXY );
}

} // namespace trsv
} // namespace El