/*
   Copyright (c) 2009-2014, Jack Poulson
   All rights reserved.

   This file is part of Elemental and is under the BSD 2-Clause License, 
   which can be found in the LICENSE file in the root directory, or at 
   http://opensource.org/licenses/BSD-2-Clause
*/
#pragma once
#ifndef EL_TWOSIDEDTRSM_UVAR5_HPP
#define EL_TWOSIDEDTRSM_UVAR5_HPP

namespace El {
namespace twotrsm {

template<typename F> 
inline void
UVar5( UnitOrNonUnit diag, Matrix<F>& A, const Matrix<F>& U )
{
    DEBUG_ONLY(
        CallStackEntry cse("twotrsm::UVar5");
        if( A.Height() != A.Width() )
            LogicError("A must be square");
        if( U.Height() != U.Width() )
            LogicError("Triangular matrices must be square");
        if( A.Height() != U.Height() )
            LogicError("A and U must be the same size");
    )
    const Int n = A.Height();
    const Int bsize = Blocksize();

    // Temporary products
    Matrix<F> Y12;

    for( Int k=0; k<n; k+=bsize )
    {
        const Int nb = Min(bsize,n-k);
 
        const IndexRange ind0( 0,    k    );
        const IndexRange ind1( k,    k+nb );
        const IndexRange ind2( k+nb, n    );

        auto A11 =       View( A, ind1, ind1 );
        auto A12 =       View( A, ind1, ind2 );
        auto A22 =       View( A, ind2, ind2 );

        auto U11 = LockedView( U, ind1, ind1 );
        auto U12 = LockedView( U, ind1, ind2 );
        auto U22 = LockedView( U, ind2, ind2 );

        // A11 := inv(U11)' A11 inv(U11)
        twotrsm::UUnb( diag, A11, U11 );

        // Y12 := A11 U12
        Zeros( Y12, A12.Height(), A12.Width() );
        Hemm( LEFT, UPPER, F(1), A11, U12, F(0), Y12 );

        // A12 := inv(U11)' A12
        Trsm( LEFT, UPPER, ADJOINT, diag, F(1), U11, A12 );

        // A12 := A12 - 1/2 Y12
        Axpy( F(-1)/F(2), Y12, A12 );

        // A22 := A22 - (A12' U12 + U12' A12)
        Her2k( UPPER, ADJOINT, F(-1), A12, U12, F(1), A22 );

        // A12 := A12 - 1/2 Y12
        Axpy( F(-1)/F(2), Y12, A12 );

        // A12 := A12 inv(U22)
        Trsm( RIGHT, UPPER, NORMAL, diag, F(1), U22, A12 );
    }
}

template<typename F> 
inline void
UVar5
( UnitOrNonUnit diag, 
  AbstractDistMatrix<F>& APre, const AbstractDistMatrix<F>& UPre )
{
    DEBUG_ONLY(
        CallStackEntry cse("twotrsm::UVar5");
        if( APre.Height() != APre.Width() )
            LogicError("A must be square");
        if( UPre.Height() != UPre.Width() )
            LogicError("Triangular matrices must be square");
        if( APre.Height() != UPre.Height() )
            LogicError("A and U must be the same size");
    )
    const Int n = APre.Height();
    const Int bsize = Blocksize();
    const Grid& g = APre.Grid();

    DistMatrix<F> A(g), U(g);
    Copy( APre, A, READ_WRITE_PROXY );
    Copy( UPre, U, READ_PROXY );

    // Temporary distributions
    DistMatrix<F,STAR,STAR> A11_STAR_STAR(g), U11_STAR_STAR(g);
    DistMatrix<F,STAR,MC  > A12_STAR_MC(g), U12_STAR_MC(g);
    DistMatrix<F,STAR,MR  > A12_STAR_MR(g), U12_STAR_MR(g);
    DistMatrix<F,STAR,VC  > A12_STAR_VC(g), U12_STAR_VC(g);
    DistMatrix<F,STAR,VR  > A12_STAR_VR(g), U12_STAR_VR(g), Y12_STAR_VR(g);
    DistMatrix<F> Y12(g);

    for( Int k=0; k<n; k+=bsize )
    {
        const Int nb = Min(bsize,n-k);

        const IndexRange ind0( 0,    k    );
        const IndexRange ind1( k,    k+nb );
        const IndexRange ind2( k+nb, n    );

        auto A11 =       View( A, ind1, ind1 );
        auto A12 =       View( A, ind1, ind2 );
        auto A22 =       View( A, ind2, ind2 );

        auto U11 = LockedView( U, ind1, ind1 );
        auto U12 = LockedView( U, ind1, ind2 );
        auto U22 = LockedView( U, ind2, ind2 );

        // A11 := inv(U11)' A11 inv(U11)
        U11_STAR_STAR = U11;
        A11_STAR_STAR = A11;
        LocalTwoSidedTrsm( UPPER, diag, A11_STAR_STAR, U11_STAR_STAR );
        A11 = A11_STAR_STAR;

        // Y12 := A11 U12
        U12_STAR_VR.AlignWith( A22 );
        U12_STAR_VR = U12;
        Y12_STAR_VR.AlignWith( A12 );
        Zeros( Y12_STAR_VR, nb, A12.Width() );
        Hemm
        ( LEFT, UPPER,
          F(1), A11_STAR_STAR.Matrix(), U12_STAR_VR.Matrix(),
          F(0), Y12_STAR_VR.Matrix() );
        Y12.AlignWith( A12 );
        Y12 = Y12_STAR_VR;

        // A12 := inv(U11)' A12
        A12_STAR_VR.AlignWith( A22 );
        A12_STAR_VR = A12;
        LocalTrsm
        ( LEFT, UPPER, ADJOINT, diag, F(1), U11_STAR_STAR, A12_STAR_VR );
        A12 = A12_STAR_VR;

        // A12 := A12 - 1/2 Y12
        Axpy( F(-1)/F(2), Y12, A12 );

        // A22 := A22 - (A12' U12 + U12' A12)
        A12_STAR_VR = A12;
        A12_STAR_VC.AlignWith( A22 );
        A12_STAR_VC = A12_STAR_VR;
        U12_STAR_VC.AlignWith( A22 );
        U12_STAR_VC = U12_STAR_VR;
        A12_STAR_MC.AlignWith( A22 );
        A12_STAR_MC = A12_STAR_VC;
        U12_STAR_MC.AlignWith( A22 );
        U12_STAR_MC = U12_STAR_VC;
        A12_STAR_MR.AlignWith( A22 );
        A12_STAR_MR = A12_STAR_VR;
        U12_STAR_MR.AlignWith( A22 );
        U12_STAR_MR = U12_STAR_VR;
        LocalTrr2k
        ( UPPER, ADJOINT, ADJOINT,
          F(-1), U12_STAR_MC, A12_STAR_MR,
                 A12_STAR_MC, U12_STAR_MR,
          F(1), A22 );

        // A12 := A12 - 1/2 Y12
        Axpy( F(-1)/F(2), Y12, A12 );

        // A12 := A12 inv(U22)
        //
        // This is the bottleneck because A12 only has blocksize rows
        Trsm( RIGHT, UPPER, NORMAL, diag, F(1), U22, A12 );
    }

    Copy( A, APre, RESTORE_READ_WRITE_PROXY );
}

} // namespace twotrsm
} // namespace El

#endif // ifndef EL_TWOSIDEDTRSM_UVAR5_HPP