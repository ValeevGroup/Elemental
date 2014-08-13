/*
   Copyright (c) 2009-2014, Jack Poulson
   All rights reserved.

   This file is part of Elemental and is under the BSD 2-Clause License, 
   which can be found in the LICENSE file in the root directory, or at 
   http://opensource.org/licenses/BSD-2-Clause
*/

namespace El {
namespace symm {

template<typename T>
void LocalAccumulateRU
( Orientation orientation, T alpha,
  const DistMatrix<T,MC,  MR  >& A,
  const DistMatrix<T,STAR,MC  >& B_STAR_MC,
  const DistMatrix<T,MR,  STAR>& BTrans_MR_STAR,
        DistMatrix<T,MC,  STAR>& ZTrans_MC_STAR,
        DistMatrix<T,MR,  STAR>& ZTrans_MR_STAR )
{
    DEBUG_ONLY(
        CallStackEntry cse("symm::LocalAccumulateRU");
        AssertSameGrids
        ( A, B_STAR_MC, BTrans_MR_STAR, ZTrans_MC_STAR, ZTrans_MR_STAR );
        if( A.Height() != A.Width() ||
            A.Height() != B_STAR_MC.Width() ||
            A.Height() != BTrans_MR_STAR.Height() ||
            A.Height() != ZTrans_MC_STAR.Height() ||
            A.Height() != ZTrans_MR_STAR.Height() ||
            B_STAR_MC.Height() != BTrans_MR_STAR.Width() ||
            BTrans_MR_STAR.Width() != ZTrans_MC_STAR.Width() ||
            ZTrans_MC_STAR.Width() != ZTrans_MR_STAR.Width() )
            LogicError
            ("Nonconformal:\n",
             DimsString(A,"A"),"\n",
             DimsString(B_STAR_MC,"B[* ,MC]"),"\n",
             DimsString(BTrans_MR_STAR,"B'[MR,* ]"),"\n",
             DimsString(ZTrans_MC_STAR,"Z'[MC,* ]"),"\n",
             DimsString(ZTrans_MR_STAR,"Z'[MR,* ]"));
        if( B_STAR_MC.RowAlign() != A.ColAlign() ||
            BTrans_MR_STAR.ColAlign() != A.RowAlign() ||
            ZTrans_MC_STAR.ColAlign() != A.ColAlign() ||
            ZTrans_MR_STAR.ColAlign() != A.RowAlign() )
            LogicError("Partial matrix distributions are misaligned");
    )
    const Int m = B_STAR_MC.Height();
    const Int n = B_STAR_MC.Width();
    const Grid& g = A.Grid();
    const Int ratio = Max( g.Height(), g.Width() );
    const Int bsize = ratio*Blocksize();

    DistMatrix<T> D11(g);

    for( Int k=0; k<n; k+=bsize )
    {
        const Int nb = Min(bsize,n-k);

        auto A11 = LockedViewRange( A, k, k,    k+nb, k+nb );
        auto A12 = LockedViewRange( A, k, k+nb, k+nb, n    );

        auto B1_STAR_MC = LockedViewRange( B_STAR_MC, 0, k,    m, k+nb );

        auto B1Trans_MR_STAR = 
            LockedViewRange( BTrans_MR_STAR, k,    0, k+nb, m );
        auto B2Trans_MR_STAR = 
            LockedViewRange( BTrans_MR_STAR, k+nb, 0, n,    m );

        auto Z1Trans_MC_STAR = ViewRange( ZTrans_MC_STAR, k,    0, k+nb, m );

        auto Z1Trans_MR_STAR = ViewRange( ZTrans_MR_STAR, k,    0, k+nb, m );
        auto Z2Trans_MR_STAR = ViewRange( ZTrans_MR_STAR, k+nb, 0, n,    m );

        D11.AlignWith( A11 );
        D11 = A11;
        MakeTriangular( UPPER, D11 );
        LocalGemm
        ( orientation, orientation,
          alpha, D11, B1_STAR_MC, T(1), Z1Trans_MR_STAR );
        SetDiagonal( D11, T(0) );

        LocalGemm
        ( NORMAL, NORMAL, alpha, D11, B1Trans_MR_STAR, T(1), Z1Trans_MC_STAR );

        LocalGemm
        ( orientation, orientation, 
          alpha, A12, B1_STAR_MC, T(1), Z2Trans_MR_STAR );

        LocalGemm
        ( NORMAL, NORMAL, alpha, A12, B2Trans_MR_STAR, T(1), Z1Trans_MC_STAR );
    }
}

template<typename T>
inline void
RUA
( T alpha, const AbstractDistMatrix<T>& APre, const AbstractDistMatrix<T>& BPre,
  T beta,        AbstractDistMatrix<T>& CPre, bool conjugate=false )
{
    DEBUG_ONLY(
        CallStackEntry cse("symm::RUA");
        AssertSameGrids( APre, BPre, CPre );
    )
    const Int m = CPre.Height();
    const Int n = CPre.Width();
    const Int bsize = Blocksize();
    const Grid& g = APre.Grid();
    const Orientation orientation = ( conjugate ? ADJOINT : TRANSPOSE );

    // Force 'A', 'B', and 'C' to be in [MC,MR] distributions
    DistMatrix<T> A(g), B(g), C(g);
    Copy( APre, A, READ_PROXY );
    Copy( BPre, B, READ_PROXY );
    Copy( CPre, C, READ_WRITE_PROXY );

    DistMatrix<T,MR,  STAR> B1Trans_MR_STAR(g);
    DistMatrix<T,VC,  STAR> B1Trans_VC_STAR(g);
    DistMatrix<T,STAR,MC  > B1_STAR_MC(g);
    DistMatrix<T,MC,  STAR> Z1Trans_MC_STAR(g);
    DistMatrix<T,MR,  STAR> Z1Trans_MR_STAR(g);
    DistMatrix<T,MC,  MR  > Z1Trans(g);
    DistMatrix<T,MR,  MC  > Z1Trans_MR_MC(g);

    B1Trans_MR_STAR.AlignWith( A );
    B1Trans_VC_STAR.AlignWith( A );
    B1_STAR_MC.AlignWith( A );
    Z1Trans_MC_STAR.AlignWith( A );
    Z1Trans_MR_STAR.AlignWith( A );

    Matrix<T> Z1Local;

    Scale( beta, C );
    for( Int k=0; k<m; k+=bsize )
    {
        const Int nb = Min(bsize,m-k);

        auto B1 = LockedView( B, k, 0, nb, n );
        auto C1 =       View( C, k, 0, nb, n );

        B1.TransposeColAllGather( B1Trans_MR_STAR, conjugate );
        B1Trans_VC_STAR = B1Trans_MR_STAR;
        B1Trans_VC_STAR.TransposePartialColAllGather( B1_STAR_MC, conjugate );
        Zeros( Z1Trans_MC_STAR, n, nb );
        Zeros( Z1Trans_MR_STAR, n, nb );
        LocalAccumulateRU
        ( orientation, alpha, A, B1_STAR_MC, B1Trans_MR_STAR, 
          Z1Trans_MC_STAR, Z1Trans_MR_STAR );

        Z1Trans.RowSumScatterFrom( Z1Trans_MC_STAR );
        Z1Trans_MR_MC.AlignWith( C1 );
        Z1Trans_MR_MC = Z1Trans;
        Z1Trans_MR_MC.RowSumScatterUpdate( T(1), Z1Trans_MR_STAR );
        Transpose( Z1Trans_MR_MC.LockedMatrix(), Z1Local, conjugate );
        Axpy( T(1), Z1Local, C1.Matrix() );
    }

    Copy( CPre, C, RESTORE_READ_WRITE_PROXY );
}

template<typename T>
inline void
RUC
( T alpha, const AbstractDistMatrix<T>& APre, const AbstractDistMatrix<T>& BPre,
  T beta,        AbstractDistMatrix<T>& CPre, bool conjugate=false )
{
    DEBUG_ONLY(
        CallStackEntry cse("symm::RUC");
        AssertSameGrids( APre, BPre, CPre );
    )
    const Int m = CPre.Height();
    const Int n = CPre.Width();
    const Int bsize = Blocksize();
    const Grid& g = APre.Grid();
    const Orientation orientation = ( conjugate ? ADJOINT : TRANSPOSE );

    // Force 'A', 'B', and 'C' to be in [MC,MR] distributions
    DistMatrix<T> A(g), B(g), C(g);
    Copy( APre, A, READ_PROXY );
    Copy( BPre, B, READ_PROXY );
    Copy( CPre, C, READ_WRITE_PROXY );

    // Temporary distributions
    DistMatrix<T,MC,  STAR> B1_MC_STAR(g);
    DistMatrix<T,VR,  STAR> AT1_VR_STAR(g);
    DistMatrix<T,STAR,MR  > AT1Trans_STAR_MR(g);
    DistMatrix<T,MR,  STAR> A1RTrans_MR_STAR(g);

    B1_MC_STAR.AlignWith( C );

    Scale( beta, C );
    for( Int k=0; k<n; k+=bsize )
    {
        const Int nb = Min(bsize,n-k);

        auto A1R = LockedViewRange( A, k, k, k+nb, n    );
        auto AT1 = LockedViewRange( A, 0, k, k+nb, k+nb );

        auto B1 = LockedViewRange( B, 0, k, m, k+nb );

        auto CLeft  = ViewRange( C, 0, 0, m, k+nb );
        auto CRight = ViewRange( C, 0, k, m, n    );

        AT1_VR_STAR.AlignWith( CLeft );
        AT1_VR_STAR = AT1;
        AT1Trans_STAR_MR.AlignWith( CLeft );
        AT1_VR_STAR.TransposePartialColAllGather( AT1Trans_STAR_MR, conjugate );
        A1RTrans_MR_STAR.AlignWith( CRight );
        A1R.TransposeColAllGather( A1RTrans_MR_STAR, conjugate );
        MakeTriangular( LOWER, A1RTrans_MR_STAR );
        MakeTrapezoidal( LOWER, AT1Trans_STAR_MR, k-1 );

        B1_MC_STAR = B1;
        LocalGemm
        ( NORMAL, orientation, 
          alpha, B1_MC_STAR, A1RTrans_MR_STAR, T(1), CRight );

        LocalGemm
        ( NORMAL, NORMAL,
          alpha, B1_MC_STAR, AT1Trans_STAR_MR, T(1), CLeft );
    }

    Copy( C, CPre, RESTORE_READ_WRITE_PROXY );
}

template<typename T>
inline void
RU
( T alpha, const AbstractDistMatrix<T>& A, const AbstractDistMatrix<T>& B,
  T beta,        AbstractDistMatrix<T>& C,
  bool conjugate=false )
{
    DEBUG_ONLY(CallStackEntry cse("symm::RU"))
    // TODO: Come up with a better routing mechanism
    if( A.Height() > 5*B.Height() )
        symm::RUA( alpha, A, B, beta, C, conjugate );
    else
        symm::RUC( alpha, A, B, beta, C, conjugate );
}

} // namespace symm
} // namespace El