#include "Camera.h"

using namespace VectorMath;

namespace CoreLib
{
	namespace Graphics
	{
		Camera::Camera()
		{
			Reset();
			CanFly = true;
		}

		void Camera::GetInverseRotationMatrix(float mat[9])
		{
			Vec3 left;
			Vec3::Cross(left, dir, up);
			Vec3::Normalize(left, left);
			mat[0] = left.x; mat[1] = up.x; mat[2] = -dir.x;
			mat[3] = left.y; mat[4] = up.y; mat[5] = -dir.y;
			mat[6] = left.z; mat[7] = up.z; mat[8] = -dir.z;
		}

		void Camera::Reset()
		{
			alpha = (float)PI;
			beta = 0.0f;
			pos = Vec3::Create(0.0f,0.0f,0.0f);
			up = Vec3::Create(0.0f,1.0f,0.0f);
			dir = Vec3::Create(0.0f,0.0f,-1.0f);
		}

		void Camera::GoForward(float u)
		{
			Vec3 vp;
			if (CanFly) 
			{
				pos += dir*u;
			}
			else
			{
				vp.x = sin(alpha);
				vp.z = cos(alpha);
				pos += vp*u;
			}
		}

		void Camera::MoveLeft(float u)
		{
			Vec3 l, vp;
			vp.x = sin(alpha);
			vp.z = cos(alpha);
			l.x=vp.z;	l.y=0;	l.z=-vp.x;
			pos += l*u;
		}

		void Camera::TurnLeft(float u)
		{
			alpha += u;
		}

		void Camera::TurnUp(float u)
		{
			beta += u;
			if (beta > (float)PI/2)
				beta=(float)PI/2;
			if (beta < (float)-PI/2)
				beta=-(float)PI/2;
		}

		void Camera::GetTransform(Matrix4 & rs)
		{
			dir = Vec3::Create((float)sin(alpha)*cos(beta),
					   (float)sin(beta),
					   (float)cos(alpha)*cos(beta));
			up = Vec3::Create((float)sin(alpha)*cos(PI/2+beta),
					  (float)sin(PI/2+beta),
					  (float)cos(alpha)*(float)cos(PI/2+beta));
			ViewFrustum view;
			GetView(view);
			rs = view.GetViewTransform();
		}

		void Camera::GetView(ViewFrustum & view)
		{
			dir = Vec3::Create((float)sin(alpha)*cos(beta),
				(float)sin(beta),
				(float)cos(alpha)*cos(beta));
			up = Vec3::Create((float)sin(alpha)*cos(PI / 2 + beta),
				(float)sin(PI / 2 + beta),
				(float)cos(alpha)*(float)cos(PI / 2 + beta));
			view.CamPos = pos;
			view.CamDir = dir;
			view.CamUp = up;
		}
	}
}