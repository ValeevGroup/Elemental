/*
   Copyright (c) 2009-2014, Jack Poulson
   All rights reserved.

   This file is part of Elemental and is under the BSD 2-Clause License, 
   which can be found in the LICENSE file in the root directory, or at 
   http://opensource.org/licenses/BSD-2-Clause
*/
#include "El.hpp"

namespace El {

void InvertPermutation( const Matrix<Int>& perm, Matrix<Int>& invPerm )
{
    DEBUG_ONLY(
        CallStackEntry cse("InvertPermutation");
        if( perm.Width() != 1 )
            LogicError("perm must be a column vector");
    )
    const Int n = perm.Height();
    invPerm.Resize( n, 1 );
    if( n == 0 )
        return;

    DEBUG_ONLY(
        // This is obviously necessary but not sufficient for 'perm' to contain
        // a reordering of (0,1,...,n-1).
        const Int range = MaxNorm( perm ) + 1;
        if( range != n )
            LogicError("Invalid permutation range");
    )

    for( Int i=0; i<n; ++i )
        invPerm.Set( perm.Get(i,0), 0, i );
}

void InvertPermutation
( const AbstractDistMatrix<Int>& permPre, AbstractDistMatrix<Int>& invPermPre )
{
    DEBUG_ONLY(
        CallStackEntry cse("InvertPermutation");
        if( permPre.Width() != 1 )
            LogicError("perm must be a column vector");
    )

    const Int n = permPre.Height();
    invPermPre.AlignWith( permPre, false );
    invPermPre.Resize( n, 1 );
    if( n == 0 )
        return;

    const Grid& g = permPre.Grid();
    DistMatrix<Int,VC,STAR> perm(g), invPerm(g);
    Copy( permPre,    perm,    READ_PROXY  );
    Copy( invPermPre, invPerm, WRITE_PROXY );

    DEBUG_ONLY(
        // This is obviously necessary but not sufficient for 'perm' to contain
        // a reordering of (0,1,...,n-1).
        const Int range = MaxNorm( perm ) + 1;
        if( range != n )
            LogicError("Invalid permutation range");
    )

    const mpi::Comm colComm = perm.ColComm();
    const Int commSize = mpi::Size( colComm );
    std::vector<int> sendCounts(commSize,0), sendDispls(commSize),
                     recvCounts(commSize,0), recvDispls(commSize);

    // Compute the send counts
    for( Int iLoc=0; iLoc<perm.LocalHeight(); ++iLoc )
    {
        const Int iDest = perm.GetLocal(iLoc,0);
        const Int owner = invPerm.RowOwner(iDest);
        sendCounts[owner] += 2; // we'll send the global index and the value
    }
    // Perform a small AllToAll to get the receive counts
    mpi::AllToAll( sendCounts.data(), 1, recvCounts.data(), 1, colComm );

    // Compute the displacements
    int sendTotal=0, recvTotal=0;
    for( Int q=0; q<commSize; ++q )
    {
        sendDispls[q] = sendTotal;
        recvDispls[q] = recvTotal;
        sendTotal += sendCounts[q];
        recvTotal += recvCounts[q];
    }

    // Pack the send data
    std::vector<Int> sendBuf(sendTotal);
    auto offsets = sendDispls;
    for( Int iLoc=0; iLoc<perm.LocalHeight(); ++iLoc )
    {
        const Int i     = perm.GlobalRow(iLoc);
        const Int iDest = perm.GetLocal(iLoc,0);
        const Int owner = invPerm.RowOwner(iDest);
        sendBuf[offsets[owner]++] = iDest;
        sendBuf[offsets[owner]++] = i;
    }

    // Perform the actual exchange
    std::vector<Int> recvBuf(recvTotal);
    mpi::AllToAll
    ( sendBuf.data(), sendCounts.data(), sendDispls.data(),
      recvBuf.data(), recvCounts.data(), recvDispls.data(), colComm );
    SwapClear( sendBuf );
    SwapClear( sendCounts );
    SwapClear( sendDispls );

    // Unpack the received data
    for( Int k=0; k<recvTotal/2; ++k )
    {
        const Int iDest = recvBuf[2*k+0];
        const Int i     = recvBuf[2*k+1];

        const Int iDestLoc = invPerm.LocalRow(iDest);
        invPerm.SetLocal( iDestLoc, 0, i );
    }

    Copy( invPerm, invPermPre, RESTORE_WRITE_PROXY );
}

} // namespace El