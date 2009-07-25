// This file is part of Eigen, a lightweight C++ template library
// for linear algebra.
//
// Copyright (C) 2009 Gael Guennebaud <g.gael@free.fr>
//
// Eigen is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 3 of the License, or (at your option) any later version.
//
// Alternatively, you can redistribute it and/or
// modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation; either version 2 of
// the License, or (at your option) any later version.
//
// Eigen is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License or the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License and a copy of the GNU General Public License along with
// Eigen. If not, see <http://www.gnu.org/licenses/>.

#ifndef EIGEN_TRIANGULAR_SOLVER_MATRIX_H
#define EIGEN_TRIANGULAR_SOLVER_MATRIX_H

template<typename Scalar, int nr>
struct ei_gemm_pack_rhs_panel
{
  enum { PacketSize = ei_packet_traits<Scalar>::size };
  void operator()(Scalar* blockB, const Scalar* rhs, int rhsStride, Scalar alpha, int depth, int cols, int stride, int offset)
  {
    int packet_cols = (cols/nr) * nr;
    int count = 0;
    for(int j2=0; j2<packet_cols; j2+=nr)
    {
      // skip what we have before
      count += PacketSize * nr * offset;
      const Scalar* b0 = &rhs[(j2+0)*rhsStride];
      const Scalar* b1 = &rhs[(j2+1)*rhsStride];
      const Scalar* b2 = &rhs[(j2+2)*rhsStride];
      const Scalar* b3 = &rhs[(j2+3)*rhsStride];
      for(int k=0; k<depth; k++)
      {
                  ei_pstore(&blockB[count+0*PacketSize], ei_pset1(alpha*b0[k]));
                  ei_pstore(&blockB[count+1*PacketSize], ei_pset1(alpha*b1[k]));
        if(nr==4) ei_pstore(&blockB[count+2*PacketSize], ei_pset1(alpha*b2[k]));
        if(nr==4) ei_pstore(&blockB[count+3*PacketSize], ei_pset1(alpha*b3[k]));
        count += nr*PacketSize;
      }
      // skip what we have after
      count += PacketSize * nr * (stride-offset-depth);
    }
    // copy the remaining columns one at a time (nr==1)
    for(int j2=packet_cols; j2<cols; ++j2)
    {
      count += PacketSize * offset;
      const Scalar* b0 = &rhs[(j2+0)*rhsStride];
      for(int k=0; k<depth; k++)
      {
        ei_pstore(&blockB[count], ei_pset1(alpha*b0[k]));
        count += PacketSize;
      }
      count += PacketSize * (stride-offset-depth);
    }
  }
};

/* Optimized triangular solver with multiple right hand side (_TRSM)
 */
template <typename Scalar,
          int LhsStorageOrder,
          int RhsStorageOrder,
          int Mode>
struct ei_triangular_solve_matrix//<Scalar,LhsStorageOrder,RhsStorageOrder>
{

  static EIGEN_DONT_INLINE void run(
    int size, int cols,
    const Scalar* _lhs, int lhsStride,
          Scalar* _rhs, int rhsStride)
  {
    ei_const_blas_data_mapper<Scalar, LhsStorageOrder> lhs(_lhs,lhsStride);
    ei_blas_data_mapper      <Scalar, RhsStorageOrder> rhs(_rhs,rhsStride);

    typedef ei_product_blocking_traits<Scalar> Blocking;
    enum {
      SmallPanelWidth   = EIGEN_ENUM_MAX(Blocking::mr,Blocking::nr),
      IsLowerTriangular = (Mode&LowerTriangular) == LowerTriangular
    };

    int kc = std::min<int>(Blocking::Max_kc/4,size); // cache block size along the K direction
    int mc = std::min<int>(Blocking::Max_mc,size);   // cache block size along the M direction

    Scalar* blockA = ei_aligned_stack_new(Scalar, kc*mc);
    Scalar* blockB = ei_aligned_stack_new(Scalar, kc*cols*Blocking::PacketSize);

    ei_gebp_kernel<Scalar, Blocking::mr, Blocking::nr, ei_conj_helper<false,false> > gebp_kernel;
    ei_gemm_pack_lhs<Scalar,Blocking::mr,LhsStorageOrder> pack_lhs;

    for(int k2=IsLowerTriangular ? 0 : size;
        IsLowerTriangular ? k2<size : k2>0;
        IsLowerTriangular ? k2+=kc : k2-=kc)
    {
      const int actual_kc = std::min(IsLowerTriangular ? size-k2 : k2, kc);

      // We have selected and packed a big horizontal panel R1 of rhs. Let B be the packed copy of this panel,
      // and R2 the remaining part of rhs. The corresponding vertical panel of lhs is split into
      // A11 (the triangular part) and A21 the remaining rectangular part.
      // Then the high level algorithm is:
      //  - B = R1                    => general block copy (done during the next step)
      //  - R1 = L1^-1 B              => tricky part
      //  - update B from the new R1  => actually this has to be performed continuously during the above step
      //  - R2 = L2 * B               => GEPP

      // The tricky part: compute R1 = L1^-1 B while updating B from R1
      // The idea is to split L1 into multiple small vertical panels.
      // Each panel can be split into a small triangular part A1 which is processed without optimization,
      // and the remaining small part A2 which is processed using gebp with appropriate block strides
      {
        // for each small vertical panels of lhs
        for (int k1=0; k1<actual_kc; k1+=SmallPanelWidth)
        {
          int actualPanelWidth = std::min<int>(actual_kc-k1, SmallPanelWidth);
          // tr solve
          for (int k=0; k<actualPanelWidth; ++k)
          {
            // TODO write a small kernel handling this (can be shared with trsv)
            int i  = IsLowerTriangular ? k2+k1+k : k2-k1-k-1;
            int s  = IsLowerTriangular ? k2+k1 : i+1;
            int rs = actualPanelWidth - k - 1; // remaining size

            Scalar a = (Mode & UnitDiagBit) ? Scalar(1) : Scalar(1)/lhs(i,i);
            for (int j=0; j<cols; ++j)
            {
              if (LhsStorageOrder==RowMajor)
              {
                Scalar b = 0;
                const Scalar* l = &lhs(i,s);
                Scalar* r = &rhs(s,j);
                for (int i3=0; i3<k; ++i3)
                  b += l[i3] * r[i3];

                rhs(i,j) = (rhs(i,j) - b)*a;
              }
              else
              {
                int s = IsLowerTriangular ? i+1 : i-rs;
                Scalar b = (rhs(i,j) *= a);
                Scalar* r = &rhs(s,j);
                const Scalar* l = &lhs(s,i);
                for (int i3=0;i3<rs;++i3)
                  r[i3] -= b * l[i3];
              }
            }
          }

          int lengthTarget = actual_kc-k1-actualPanelWidth;
          int startBlock   = IsLowerTriangular ? k2+k1 : k2-k1-actualPanelWidth;
          int blockBOffset = IsLowerTriangular ? k1 : lengthTarget;

          // update the respective rows of B from rhs
          ei_gemm_pack_rhs_panel<Scalar, Blocking::nr>()
            (blockB, _rhs+startBlock, rhsStride, -1, actualPanelWidth, cols, actual_kc, blockBOffset);

          // GEBP
          if (lengthTarget>0)
          {
            int startTarget  = IsLowerTriangular ? k2+k1+actualPanelWidth : k2-actual_kc;

            pack_lhs(blockA, &lhs(startTarget,startBlock), lhsStride, actualPanelWidth, lengthTarget);

            gebp_kernel(_rhs+startTarget, rhsStride, blockA, blockB, lengthTarget, actualPanelWidth, cols,
                        actualPanelWidth, actual_kc, 0, blockBOffset*Blocking::PacketSize);
          }
        }
      }

      // R2 = A2 * B => GEPP
      {
        int start = IsLowerTriangular ? k2+kc : 0;
        int end   = IsLowerTriangular ? size : k2-kc;
        for(int i2=start; i2<end; i2+=mc)
        {
          const int actual_mc = std::min(mc,end-i2);
          if (actual_mc>0)
          {
            pack_lhs(blockA, &lhs(i2, IsLowerTriangular ? k2 : k2-kc), lhsStride, actual_kc, actual_mc);

            gebp_kernel(_rhs+i2, rhsStride, blockA, blockB, actual_mc, actual_kc, cols);
          }
        }
      }
    }

    ei_aligned_stack_delete(Scalar, blockA, kc*mc);
    ei_aligned_stack_delete(Scalar, blockB, kc*cols*Blocking::PacketSize);
  }
};

#endif // EIGEN_TRIANGULAR_SOLVER_MATRIX_H
