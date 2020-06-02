#include "Skeleton.h"
#include "CoreLib/LibMath.h"

namespace GameEngine
{
	using namespace CoreLib::IO;
	
	BoneTransformation AnimationChannel::Sample(float animTime)
	{
		BoneTransformation result;
		int frame0 = BinarySearchForKeyFrame(KeyFrames, animTime);
		int frame1 = frame0 + 1;
		float t = 0.0f;
		if (frame0 < KeyFrames.Count() - 1)
		{
			t = (animTime - KeyFrames[frame0].Time) / (KeyFrames[frame1].Time - KeyFrames[frame0].Time);
		}
		else
		{
			t = 0.0f;
			frame1 = 0;
		}
		auto & f0 = KeyFrames[frame0];
		auto & f1 = KeyFrames[frame1];
		return BoneTransformation::Lerp(f0.Transform, f1.Transform, t);

	}

	float BlendShapeAnimationChannel::Sample(float animTime)
    {
        int frame0 = BinarySearchForKeyFrame(KeyFrames, animTime);
        int frame1 = frame0 + 1;
        float t = 0.0f;
        if (frame0 < KeyFrames.Count() - 1)
        {
            t = (animTime - KeyFrames[frame0].Time) / (KeyFrames[frame1].Time - KeyFrames[frame0].Time);
        }
        else
        {
            t = 0.0f;
            frame1 = 0;
        }
        auto &f0 = KeyFrames[frame0];
        auto &f1 = KeyFrames[frame1];
        return f0.Weight * (1.0f - t) + f1.Weight * t;
    }

    Skeleton Skeleton::TopologySort()
    {
        Skeleton result;
        CoreLib::List<int> workList;
        CoreLib::HashSet<int> visitedBones;
        for (int i = 0; i < Bones.Count(); i++)
        {
            if (Bones[i].ParentId == -1)
            {
                visitedBones.Add(i);
                workList.Add(i);
            }
        }
        CoreLib::Dictionary<int, int> boneMapping;
        for (int i = 0; i < workList.Count(); i++)
        {
            int originalBoneId = workList[i];
            boneMapping[originalBoneId] = result.Bones.Count();
            result.Bones.Add(Bones[originalBoneId]);
            result.InversePose.Add(InversePose[originalBoneId]);
            for (int j = 0; j < Bones.Count(); j++)
            {
                if (visitedBones.Contains(Bones[j].ParentId))
                {
                    if (visitedBones.Add(j))
                        workList.Add(j);
                }
            }
        }
		
        for (auto & bone : result.Bones)
            bone.ParentId = bone.ParentId == -1?-1:boneMapping[bone.ParentId]();
		
        for (auto & mapping : BoneMapping)
            result.BoneMapping[mapping.Key] = boneMapping[mapping.Value]();

        return result;
    }

    void Skeleton::SaveToStream(CoreLib::IO::Stream * stream)
	{
		BinaryWriter writer(stream);
		writer.Write(this->Name);
		writer.Write(this->Bones.Count());
		for (int i = 0; i < Bones.Count(); i++)
		{
			writer.Write(Bones[i].Name);
			writer.Write(Bones[i].ParentId);
			writer.Write(InversePose[i]);
			writer.Write(Bones[i].BindPose);
		}
		writer.ReleaseStream();
	}

	void Skeleton::LoadFromStream(CoreLib::IO::Stream * stream)
	{
		BinaryReader reader(stream);
		Name = reader.ReadString();
		int boneCount = reader.ReadInt32();
		Bones.SetSize(boneCount);
		InversePose.SetSize(boneCount);
		BoneMapping.Clear();
		for (int i = 0; i < Bones.Count(); i++)
		{
			reader.Read(Bones[i].Name);
			reader.Read(Bones[i].ParentId);
			reader.Read(InversePose[i]);
			reader.Read(Bones[i].BindPose);
			BoneMapping[Bones[i].Name] = i;
		}
		reader.ReleaseStream();
	}

	void Skeleton::SaveToFile(const String & file)
	{
		RefPtr<FileStream> stream = new FileStream(file, FileMode::Create);
		SaveToStream(stream.Ptr());
		stream->Close();
	}

	void Skeleton::LoadFromFile(const String & file)
	{
		RefPtr<FileStream> stream = new FileStream(file, FileMode::Open);
		LoadFromStream(stream.Ptr());
		stream->Close();
	}
	void SkeletalAnimation::SaveToStream(CoreLib::IO::Stream * stream)
	{
		BinaryWriter writer(stream);
		writer.Write(Name);
		writer.Write(Speed);
		writer.Write(FPS);
		writer.Write(Duration);
        writer.Write(BlendShapeChannels.Count());
		writer.Write(Reserved);
		writer.Write(Channels.Count());
		for (int i = 0; i < Channels.Count(); i++)
		{
			writer.Write(Channels[i].BoneName);
			writer.Write(Channels[i].KeyFrames);
		}
        for (int i = 0; i < BlendShapeChannels.Count(); i++)
        {
            writer.Write(BlendShapeChannels[i].Name);
            writer.Write(BlendShapeChannels[i].KeyFrames);
        }
		writer.ReleaseStream();
	}
	void SkeletalAnimation::LoadFromStream(CoreLib::IO::Stream * stream)
	{
		BinaryReader reader(stream);
		reader.Read(Name);
		reader.Read(Speed);
		reader.Read(FPS);
		reader.Read(Duration);
        BlendShapeChannelCount = reader.ReadInt32();
        BlendShapeChannels.SetSize(BlendShapeChannelCount);
		reader.Read(Reserved);
		int channelCount = reader.ReadInt32();
		Channels.SetSize(channelCount);
		for (int i = 0; i < Channels.Count(); i++)
		{
			reader.Read(Channels[i].BoneName);
			reader.Read(Channels[i].KeyFrames);
		}
        for (int i = 0; i < BlendShapeChannels.Count(); i++)
        {
            reader.Read(BlendShapeChannels[i].Name);
            reader.Read(BlendShapeChannels[i].KeyFrames);
        }
		reader.ReleaseStream();
	}
	void SkeletalAnimation::SaveToFile(const CoreLib::String & filename)
	{
		RefPtr<FileStream> stream = new FileStream(filename, FileMode::Create);
		SaveToStream(stream.Ptr());
		stream->Close();
	}
	void SkeletalAnimation::LoadFromFile(const CoreLib::String & filename)
	{
		RefPtr<FileStream> stream = new FileStream(filename, FileMode::Open);
		LoadFromStream(stream.Ptr());
		stream->Close();
	}
	void RetargetFile::SaveToStream(CoreLib::IO::Stream * stream)
	{
		BinaryWriter writer(stream);
		writer.Write(SourceSkeletonName);
		writer.Write(TargetSkeletonName);
		writer.Write(RootTranslationScale);
        writer.Write(RetargetedInversePose.Count());
		writer.Write(RetargetedInversePose.Buffer(), RetargetedInversePose.Count());
		writer.Write(ModelBoneIdToAnimationBoneId.Buffer(), ModelBoneIdToAnimationBoneId.Count());
		writer.ReleaseStream();
	}
	void RetargetFile::LoadFromStream(CoreLib::IO::Stream * stream)
	{
		BinaryReader reader(stream);
		reader.Read(SourceSkeletonName);
		reader.Read(TargetSkeletonName);
		reader.Read(RootTranslationScale);
		int retargetTransformCount;
		reader.Read(retargetTransformCount);
		RetargetedInversePose.SetSize(retargetTransformCount);
		reader.Read(RetargetedInversePose.Buffer(), RetargetedInversePose.Count());
		PreRotations.SetSize(retargetTransformCount);
        reader.Read(PreRotations.Buffer(), PreRotations.Count());
		ModelBoneIdToAnimationBoneId.SetSize(retargetTransformCount);
		reader.Read(ModelBoneIdToAnimationBoneId.Buffer(), ModelBoneIdToAnimationBoneId.Count());
		MaxAnimationBoneId = 0;
		for (auto & id : ModelBoneIdToAnimationBoneId)
			MaxAnimationBoneId = CoreLib::Math::Max(MaxAnimationBoneId, id);
		reader.ReleaseStream();
	}
	void RetargetFile::SaveToFile(const CoreLib::String & filename)
	{
		RefPtr<FileStream> stream = new FileStream(filename, FileMode::Create);
		SaveToStream(stream.Ptr());
		stream->Close();
	}
	void RetargetFile::LoadFromFile(const CoreLib::String & filename)
	{
		RefPtr<FileStream> stream = new FileStream(filename, FileMode::Open);
		LoadFromStream(stream.Ptr());
		stream->Close();
	}
	void RetargetFile::SetBoneCount(int count)
	{
		RetargetedInversePose.SetSize(count);
        ModelBoneIdToAnimationBoneId.SetSize(count);
		int i = 0;
		for (auto & id : ModelBoneIdToAnimationBoneId)
		{
			id = i;
			i++;
		}
	}

	// used in Rendering
	void Pose::GetMatrices(const Skeleton * skeleton, CoreLib::List<VectorMath::Matrix4>& matrices, bool multiplyInversePose, RetargetFile * retarget) const
	{
		matrices.Clear();
		matrices.SetSize(skeleton->Bones.Count());

		// Consider the case that the skeleton for animation may contain less bones than the skeleton for mesh
		// So, we should skip those missing bones and using its bind pose
		for (int i = 0; i < matrices.Count(); i++)
		{
            if (retarget)
            {
                VectorMath::Matrix4::CreateIdentityMatrix(matrices[i]);
                matrices[i].SetTranslation(retarget->RetargetedTranslations[i]);
            }
            else
            {
				matrices[i] = skeleton->Bones[i].BindPose.ToMatrix();
            }
		}

		for (int i = 0; i < skeleton->BoneMapping.Count(); i++)
		{
			BoneTransformation transform;
			int targetId = i;
			if (retarget)
			{
				targetId = retarget->ModelBoneIdToAnimationBoneId[i];
                if (targetId != -1)
                    transform = Transforms[targetId];
                if (i == 0)
                {
                    transform.Translation.x *= retarget->RootTranslationScale.x;
                    transform.Translation.y *= retarget->RootTranslationScale.y;
                    transform.Translation.z *= retarget->RootTranslationScale.z;
                }
                else
                    transform.Translation.SetZero();
                transform.Translation += retarget->RetargetedTranslations[i];
                matrices[i] = (retarget->PreRotations[i] * transform.Rotation).ToMatrix4();
                matrices[i].SetTranslation(transform.Translation);
			}
			else
			{
				transform = Transforms[i];
				if (i != 0)
					transform.Translation = skeleton->Bones[i].BindPose.Translation;
                matrices[i] = transform.ToMatrix() * matrices[i];
			}
		}
		for (int i = 1; i < skeleton->Bones.Count(); i++)
		{
			VectorMath::Matrix4::Multiply(matrices[i], matrices[skeleton->Bones[i].ParentId], matrices[i]);
		}

		if (multiplyInversePose)
		{
			if (retarget)
			{
				for (int i = 0; i < skeleton->Bones.Count(); i++)
					VectorMath::Matrix4::Multiply(matrices[i], matrices[i], retarget->RetargetedInversePose[i]);
			}
			else
			{
				for (int i = 0; i < matrices.Count(); i++)
					VectorMath::Matrix4::Multiply(matrices[i], matrices[i], skeleton->InversePose[i]);
			}
		}
	}
}
