// This file is part of Eigen, a lightweight C++ template library
// for linear algebra.
//
// Copyright (C) 2015 Tal Hadad <tal_hd@hotmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla
// Public License v. 2.0. If a copy of the MPL was not distributed
// with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef EIGEN_EULERSYSTEM_H
#define EIGEN_EULERSYSTEM_H

namespace Eigen
{
  // Forward declerations
  template <typename _Scalar, class _System>
  class EulerAngles;
  
  namespace internal
  {
    // TODO: Check if already exists on the rest API
    template <int Num, bool IsPositive = (Num > 0)>
    struct Abs
    {
      enum { value = Num };
    };
  
    template <int Num>
    struct Abs<Num, false>
    {
      enum { value = -Num };
    };

    template <int Axis>
    struct IsValidAxis
    {
      enum { value = Axis != 0 && Abs<Axis>::value <= 3 };
    };
  }
  
  enum EulerAxis
  {
    EULER_X = 1,
    EULER_Y = 2,
    EULER_Z = 3
  };

  template <int _AlphaAxis, int _BetaAxis, int _GammaAxis>
  class EulerSystem
  {
    public:
    // It's defined this way and not as enum, because I think
    //  that enum is not guerantee to support negative numbers
    static const int AlphaAxis = _AlphaAxis;
    static const int BetaAxis = _BetaAxis;
    static const int GammaAxis = _GammaAxis;

    enum
    {
      AlphaAxisAbs = internal::Abs<AlphaAxis>::value,
      BetaAxisAbs = internal::Abs<BetaAxis>::value,
      GammaAxisAbs = internal::Abs<GammaAxis>::value,
      
      IsAlphaOpposite = (AlphaAxis < 0) ? 1 : 0,
      IsBetaOpposite = (BetaAxis < 0) ? 1 : 0,
      IsGammaOpposite = (GammaAxis < 0) ? 1 : 0,
      
      IsOdd = ((AlphaAxisAbs)%3 == (BetaAxisAbs - 1)%3) ? 0 : 1,
      IsEven = IsOdd ? 0 : 1,
      
      // TODO: Assert this, and sort it in a better way
      IsValid = ((unsigned)AlphaAxisAbs != (unsigned)BetaAxisAbs &&
        (unsigned)BetaAxisAbs != (unsigned)GammaAxisAbs &&
        internal::IsValidAxis<AlphaAxis>::value && internal::IsValidAxis<BetaAxis>::value && internal::IsValidAxis<GammaAxis>::value) ? 1 : 0,

      // TODO: After a proper assertation, remove the "IsValid" from this expression
      IsTaitBryan = (IsValid && (unsigned)AlphaAxisAbs != (unsigned)GammaAxisAbs) ? 1 : 0
    };

    private:

    enum
    {
      // I, J, K are the pivot indexes permutation for the rotation matrix, that match this euler system. 
      // They are used in this class converters.
      // They are always different from each other, and their possible values are: 0, 1, or 2.
      I = AlphaAxisAbs - 1,
      J = (AlphaAxisAbs - 1 + 1 + IsOdd)%3,
      K = (AlphaAxisAbs - 1 + 2 - IsOdd)%3
    };
    
    // TODO: Get @mat parameter in form that avoids double evaluation.
    template <typename Derived>
    static void CalcEulerAngles_imp(Matrix<typename MatrixBase<Derived>::Scalar, 3, 1>& res, const MatrixBase<Derived>& mat, internal::true_type /*isTaitBryan*/)
    {
      using std::atan2;
      using std::sin;
      using std::cos;
      
      typedef typename Derived::Scalar Scalar;
      typedef Matrix<Scalar,2,1> Vector2;
      
      res[0] = atan2(mat(J,K), mat(K,K));
      Scalar c2 = Vector2(mat(I,I), mat(I,J)).norm();
      if((IsOdd && res[0]<Scalar(0)) || ((!IsOdd) && res[0]>Scalar(0))) {
        res[0] = (res[0] > Scalar(0)) ? res[0] - Scalar(M_PI) : res[0] + Scalar(M_PI);
        res[1] = atan2(-mat(I,K), -c2);
      }
      else
        res[1] = atan2(-mat(I,K), c2);
      Scalar s1 = sin(res[0]);
      Scalar c1 = cos(res[0]);
      res[2] = atan2(s1*mat(K,I)-c1*mat(J,I), c1*mat(J,J) - s1 * mat(K,J));
    }

    template <typename Derived>
    static void CalcEulerAngles_imp(Matrix<typename MatrixBase<Derived>::Scalar,3,1>& res, const MatrixBase<Derived>& mat, internal::false_type /*isTaitBryan*/)
    {
      using std::atan2;
      using std::sin;
      using std::cos;

      typedef typename Derived::Scalar Scalar;
      typedef Matrix<Scalar,2,1> Vector2;
      
      res[0] = atan2(mat(J,I), mat(K,I));
      if((IsOdd && res[0]<Scalar(0)) || ((!IsOdd) && res[0]>Scalar(0)))
      {
        res[0] = (res[0] > Scalar(0)) ? res[0] - Scalar(M_PI) : res[0] + Scalar(M_PI);
        Scalar s2 = Vector2(mat(J,I), mat(K,I)).norm();
        res[1] = -atan2(s2, mat(I,I));
      }
      else
      {
        Scalar s2 = Vector2(mat(J,I), mat(K,I)).norm();
        res[1] = atan2(s2, mat(I,I));
      }

      // With a=(0,1,0), we have i=0; j=1; k=2, and after computing the first two angles,
      // we can compute their respective rotation, and apply its inverse to M. Since the result must
      // be a rotation around x, we have:
      //
      //  c2  s1.s2 c1.s2                   1  0   0 
      //  0   c1    -s1       *    M    =   0  c3  s3
      //  -s2 s1.c2 c1.c2                   0 -s3  c3
      //
      //  Thus:  m11.c1 - m21.s1 = c3  &   m12.c1 - m22.s1 = s3

      Scalar s1 = sin(res[0]);
      Scalar c1 = cos(res[0]);
      res[2] = atan2(c1*mat(J,K)-s1*mat(K,K), c1*mat(J,J) - s1 * mat(K,J));
    }

    public:
    
    template<typename Scalar>
    static void CalcEulerAngles(
      EulerAngles<Scalar, EulerSystem>& res,
      const typename EulerAngles<Scalar, EulerSystem>::Matrix3& mat)
    {
      CalcEulerAngles(res, mat, false, false, false);
    }
    
    template<
      typename Scalar,
      bool PositiveRangeAlpha,
      bool PositiveRangeBeta,
      bool PositiveRangeGamma>
    static void CalcEulerAngles(
      EulerAngles<Scalar, EulerSystem>& res,
      const typename EulerAngles<Scalar, EulerSystem>::Matrix3& mat)
    {
      CalcEulerAngles(res, mat, PositiveRangeAlpha, PositiveRangeBeta, PositiveRangeGamma);
    }
    
    template<typename Scalar>
    static void CalcEulerAngles(
      EulerAngles<Scalar, EulerSystem>& res,
      const typename EulerAngles<Scalar, EulerSystem>::Matrix3& mat,
      bool PositiveRangeAlpha,
      bool PositiveRangeBeta,
      bool PositiveRangeGamma)
    {
      CalcEulerAngles_imp(
        res.angles(), mat,
        typename internal::conditional<IsTaitBryan, internal::true_type, internal::false_type>::type());

      if (IsAlphaOpposite == IsOdd)
        res.alpha() = -res.alpha();
        
      if (IsBetaOpposite == IsOdd)
        res.beta() = -res.beta();
        
      if (IsGammaOpposite == IsOdd)
        res.gamma() = -res.gamma();
      
      // Saturate results to the requested range
      if (PositiveRangeAlpha && (res.alpha() < 0))
        res.alpha() += Scalar(2 * EIGEN_PI);
      
      if (PositiveRangeBeta && (res.beta() < 0))
        res.beta() += Scalar(2 * EIGEN_PI);
      
      if (PositiveRangeGamma && (res.gamma() < 0))
        res.gamma() += Scalar(2 * EIGEN_PI);
    }
  };

  typedef EulerSystem<EULER_X, EULER_Y, EULER_Z> EulerSystemXYZ;
  typedef EulerSystem<EULER_X, EULER_Y, EULER_X> EulerSystemXYX;
  typedef EulerSystem<EULER_X, EULER_Z, EULER_Y> EulerSystemXZY;
  typedef EulerSystem<EULER_X, EULER_Z, EULER_X> EulerSystemXZX;

  typedef EulerSystem<EULER_Y, EULER_Z, EULER_X> EulerSystemYZX;
  typedef EulerSystem<EULER_Y, EULER_Z, EULER_Y> EulerSystemYZY;
  typedef EulerSystem<EULER_Y, EULER_X, EULER_Z> EulerSystemYXZ;
  typedef EulerSystem<EULER_Y, EULER_X, EULER_Y> EulerSystemYXY;

  typedef EulerSystem<EULER_Z, EULER_X, EULER_Y> EulerSystemZXY;
  typedef EulerSystem<EULER_Z, EULER_X, EULER_Z> EulerSystemZXZ;
  typedef EulerSystem<EULER_Z, EULER_Y, EULER_X> EulerSystemZYX;
  typedef EulerSystem<EULER_Z, EULER_Y, EULER_Z> EulerSystemZYZ;
}

#endif // EIGEN_EULERSYSTEM_H
