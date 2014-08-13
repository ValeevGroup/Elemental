/*
   Copyright (c) 2009-2014, Jack Poulson
   All rights reserved.

   This file is part of Elemental and is under the BSD 2-Clause License, 
   which can be found in the LICENSE file in the root directory, or at 
   http://opensource.org/licenses/BSD-2-Clause
*/
#ifndef EL_TRRK_NT_HPP
#define EL_TRRK_NT_HPP

namespace El {
namespace trrk {

// Distributed C := alpha A B^{T/H} + beta C
template<typename T>
void TrrkNT
( UpperOrLower uplo,
  Orientation orientationOfB,
  T alpha, const AbstractDistMatrix<T>& APre,
           const AbstractDistMatrix<T>& BPre,
  T beta,        AbstractDistMatrix<T>& CPre )
{
    DEBUG_ONLY(
        CallStackEntry cse("trrk::TrrkNT");
        if( CPre.Height() != CPre.Width() || APre.Height() != CPre.Height() || 
            BPre.Height() != CPre.Width() || APre.Width() != BPre.Width() )
            LogicError("Nonconformal TrrkNT");
        if( orientationOfB == NORMAL )
            LogicError("Orientation must be ADJOINT or TRANSPOSE");
    )
    const Int n = CPre.Height();
    const Int r = APre.Width();
    const Int bsize = Blocksize();
    const Grid& g = CPre.Grid();

    DistMatrix<T> A(g), B(g), C(g);
    Copy( APre, A, READ_PROXY );
    Copy( BPre, B, READ_PROXY );
    Copy( CPre, C, READ_WRITE_PROXY );

    DistMatrix<T,MC,  STAR> A1_MC_STAR(g);
    DistMatrix<T,VR,  STAR> B1_VR_STAR(g);
    DistMatrix<T,STAR,MR  > B1Trans_STAR_MR(g);

    A1_MC_STAR.AlignWith( C );
    B1_VR_STAR.AlignWith( C );
    B1Trans_STAR_MR.AlignWith( C );

    const IndexRange outerInd( 0, n );
    for( Int k=0; k<r; k+=bsize )
    {
        const Int nb = Min(bsize,r-k);

        const IndexRange ind1( k, k+nb );

        auto A1 = LockedView( A, outerInd, ind1 );
        auto B1 = LockedView( B, outerInd, ind1 );

        A1_MC_STAR = A1;
        B1_VR_STAR = B1;
        B1_VR_STAR.TransposePartialColAllGather
        ( B1Trans_STAR_MR, (orientationOfB==ADJOINT) );
        LocalTrrk( uplo, alpha, A1_MC_STAR, B1Trans_STAR_MR, beta, C );
    }
    Copy( C, CPre, RESTORE_READ_WRITE_PROXY );
}

} // namespace trrk
} // namespace El

#endif // ifndef EL_TRRK_NT_HPP