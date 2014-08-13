/*
   Copyright (c) 2009-2014, Jack Poulson
   All rights reserved.

   This file is part of Elemental and is under the BSD 2-Clause License, 
   which can be found in the LICENSE file in the root directory, or at 
   http://opensource.org/licenses/BSD-2-Clause
*/
#pragma once
#ifndef EL_TWOSIDEDTRSM_LVAR2_HPP
#define EL_TWOSIDEDTRSM_LVAR2_HPP

namespace El {
namespace twotrsm {

template<typename F>
inline void
LVar2( UnitOrNonUnit diag, Matrix<F>& A, const Matrix<F>& L )
{
    DEBUG_ONLY(
        CallStackEntry cse("twotrsm::LVar2");
        if( A.Height() != A.Width() )
            LogicError("A must be square");
        if( L.Height() != L.Width() )
            LogicError("Triangular matrices must be square");
        if( A.Height() != L.Height() )
            LogicError("A and L must be the same size");
    )
    const Int n = A.Height();
    const Int bsize = Blocksize();

    // Temporary products
    Matrix<F> Y10, X11;

    for( Int k=0; k<n; k+=bsize )
    {
        const Int nb = Min(bsize,n-k);

        const IndexRange ind0( 0,    k    );
        const IndexRange ind1( k,    k+nb );
        const IndexRange ind2( k+nb, n    );

        auto A00 = LockedView( A, ind0, ind0 );
        auto A10 =       View( A, ind1, ind0 );
        auto A11 =       View( A, ind1, ind1 );
        auto A20 = LockedView( A, ind2, ind0 );
        auto A21 =       View( A, ind2, ind1 );

        auto L10 = LockedView( L, ind1, ind0 );
        auto L11 = LockedView( L, ind1, ind1 );

        // Y10 := L10 A00
        Zeros( Y10, nb, k );
        Hemm( RIGHT, LOWER, F(1), A00, L10, F(0), Y10 );

        // A10 := A10 - 1/2 Y10
        Axpy( F(-1)/F(2), Y10, A10 );

        // A11 := A11 - (A10 L10' + L10 A10')
        Her2k( LOWER, NORMAL, F(-1), A10, L10, F(1), A11 );

        // A11 := inv(L11) A11 inv(L11)'
        twotrsm::LUnb( diag, A11, L11 );

        // A21 := A21 - A20 L10'
        Gemm( NORMAL, ADJOINT, F(-1), A20, L10, F(1), A21 );

        // A21 := A21 inv(L11)'
        Trsm( RIGHT, LOWER, ADJOINT, diag, F(1), L11, A21 );

        // A10 := A10 - 1/2 Y10
        Axpy( F(-1)/F(2), Y10, A10 );

        // A10 := inv(L11) A10
        Trsm( LEFT, LOWER, NORMAL, diag, F(1), L11, A10 );
    }
}

// This routine has only partially been optimized. The ReduceScatter operations
// need to be (conjugate-)transposed in order to play nice with cache.
template<typename F>
inline void
LVar2
( UnitOrNonUnit diag, 
  AbstractDistMatrix<F>& A, const AbstractDistMatrix<F>& L )
{
    DEBUG_ONLY(
        CallStackEntry cse("twotrsm::LVar2");
        if( APre.Height() != APre.Width() )
            LogicError("A must be square");
        if( LPre.Height() != LPre.Width() )
            LogicError("Triangular matrices must be square");
        if( APre.Height() != LPre.Height() )
            LogicError("A and L must be the same size");
    )
    const Int n = APre.Height();
    const Int bsize = Blocksize();
    const Grid& g = APre.Grid();

    DistMatrix<F> A(g), L(g);
    Copy( APre, A, READ_WRITE_PROXY );
    Copy( LPre, L, READ_PROXY );
    
    // Temporary distributions
    DistMatrix<F,STAR,VR  > A10_STAR_VR(g);
    DistMatrix<F,STAR,STAR> A11_STAR_STAR(g), L11_STAR_STAR(g);
    DistMatrix<F,VC,  STAR> L10Adj_VC_STAR(g), A21_VC_STAR(g);
    DistMatrix<F,MR,  STAR> A10Adj_MR_STAR(g), L10Adj_MR_STAR(g), 
                            F10Adj_MR_STAR(g);
    DistMatrix<F,STAR,MC  > L10_STAR_MC(g);
    DistMatrix<F,MC,  STAR> Y10Adj_MC_STAR(g), X11_MC_STAR(g), X21_MC_STAR(g);
    DistMatrix<F,MR,  MC  > Y10Adj_MR_MC(g);
    DistMatrix<F> Y10Adj(g), X11(g);

    Matrix<F> Y10Local;

    for( Int k=0; k<n; k+=bsize )
    {
        const Int nb = Min(bsize,n-k);

        const IndexRange ind0( 0,    k    );
        const IndexRange ind1( k,    k+nb );
        const IndexRange ind2( k+nb, n    );

        auto A00 = LockedView( A, ind0, ind0 );
        auto A10 =       View( A, ind1, ind0 );
        auto A11 =       View( A, ind1, ind1 );
        auto A20 = LockedView( A, ind2, ind0 );
        auto A21 =       View( A, ind2, ind1 );

        auto L10 = LockedView( L, ind1, ind0 );
        auto L11 = LockedView( L, ind1, ind1 );

        // Y10 := L10 A00
        L10Adj_MR_STAR.AlignWith( A00 );
        L10.AdjointColAllGather( L10Adj_MR_STAR );
        L10Adj_VC_STAR.AlignWith( A00 );
        L10Adj_VC_STAR = L10Adj_MR_STAR;
        L10_STAR_MC.AlignWith( A00 );
        L10Adj_VC_STAR.AdjointPartialColAllGather( L10_STAR_MC );
        Y10Adj_MC_STAR.AlignWith( A00 );
        F10Adj_MR_STAR.AlignWith( A00 );
        Zeros( Y10Adj_MC_STAR, k, nb );
        Zeros( F10Adj_MR_STAR, k, nb );
        symm::LocalAccumulateRL
        ( ADJOINT,
          F(1), A00, L10_STAR_MC, L10Adj_MR_STAR, 
          Y10Adj_MC_STAR, F10Adj_MR_STAR );
        Y10Adj.RowSumScatterFrom( Y10Adj_MC_STAR );
        Y10Adj_MR_MC.AlignWith( A10 );
        Y10Adj_MR_MC = Y10Adj;
        Y10Adj_MR_MC.RowSumScatterUpdate( F(1), F10Adj_MR_STAR );
        Adjoint( Y10Adj_MR_MC.LockedMatrix(), Y10Local );

        // X11 := A10 L10'
        X11_MC_STAR.AlignWith( L10 );
        LocalGemm( NORMAL, NORMAL, F(1), A10, L10Adj_MR_STAR, X11_MC_STAR );

        // A10 := A10 - Y10
        Axpy( F(-1), Y10Local, A10.Matrix() );
        A10Adj_MR_STAR.AlignWith( L10 );
        A10.AdjointColAllGather( A10Adj_MR_STAR );
        
        // A11 := A11 - (X11 + L10 A10') = A11 - (A10 L10' + L10 A10')
        LocalGemm
        ( NORMAL, NORMAL, F(1), L10, A10Adj_MR_STAR, F(1), X11_MC_STAR );
        X11.AlignWith( A11 );
        X11.RowSumScatterFrom( X11_MC_STAR );
        AxpyTriangle( LOWER, F(-1), X11, A11 );

        // A10 := inv(L11) A10
        L11_STAR_STAR = L11;
        A10_STAR_VR.AdjointPartialRowFilterFrom( A10Adj_MR_STAR );
        LocalTrsm
        ( LEFT, LOWER, NORMAL, diag, F(1), L11_STAR_STAR, A10_STAR_VR );
        A10 = A10_STAR_VR;

        // A11 := inv(L11) A11 inv(L11)'
        A11_STAR_STAR = A11;
        LocalTwoSidedTrsm( LOWER, diag, A11_STAR_STAR, L11_STAR_STAR );
        A11 = A11_STAR_STAR;

        // A21 := A21 - A20 L10'
        X21_MC_STAR.AlignWith( A20 );
        LocalGemm( NORMAL, NORMAL, F(1), A20, L10Adj_MR_STAR, X21_MC_STAR );
        A21.RowSumScatterUpdate( F(-1), X21_MC_STAR );

        // A21 := A21 inv(L11)'
        A21_VC_STAR =  A21;
        LocalTrsm
        ( RIGHT, LOWER, ADJOINT, diag, F(1), L11_STAR_STAR, A21_VC_STAR );
        A21 = A21_VC_STAR;
    }

    Copy( A, APre, RESTORE_READ_WRITE_PROXY );
}

} // namespace twotrsm
} // namespace El

#endif // ifndef EL_TWOSIDEDTRSM_LVAR2_HPP