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
        writer.Write(header);
		writer.Write(SourceSkeletonName);
		writer.Write(TargetSkeletonName);
		writer.Write(RootTranslationScale);
        writer.Write(RetargetedBindPose);
        writer.Write(RetargetedInversePose);
        writer.Write(PreRotations);
        writer.Write(PostRotations);
        writer.Write(ModelBoneIdToAnimationBoneId);
        writer.Write(MorphStates.Count());
        for (auto &ms : MorphStates)
        {
            writer.Write(ms.Name);
            writer.Write(ms.BoneStates);
        }
		writer.ReleaseStream();
	}
	void RetargetFile::LoadFromStream(CoreLib::IO::Stream * stream)
	{
		BinaryReader reader(stream);
        reader.Read(header);
        if (header.Identifier != RetargetFileIdentifier)
        {
            throw CoreLib::InvalidOperationException("Invalid retarget file.");
        }
		reader.Read(SourceSkeletonName);
		reader.Read(TargetSkeletonName);
		reader.Read(RootTranslationScale);
        reader.Read(RetargetedBindPose);
		reader.Read(RetargetedInversePose);
        reader.Read(PreRotations);
        reader.Read(PostRotations);
        reader.Read(ModelBoneIdToAnimationBoneId);
        int morphStateCount = reader.ReadInt32();
        MorphStates.SetSize(morphStateCount);
        for (auto &ms : MorphStates)
        {
            reader.Read(ms.Name);
            reader.Read(ms.BoneStates);
        }

        if (PostRotations.Count() != header.SourceBoneCount ||
            RetargetedInversePose.Count() != header.SourceBoneCount ||
            ModelBoneIdToAnimationBoneId.Count() != header.SourceBoneCount ||
            RetargetedBindPose.Count() != header.SourceBoneCount)
        {
            throw CoreLib::InvalidOperationException("Invalid retarget file. Retarget data does not match skeleton size.");
        }

        for (auto &id : ModelBoneIdToAnimationBoneId)
        {
            if (id != -1 && id >= header.TargetBoneCount)
            {
                throw CoreLib::InvalidOperationException("Invalid retarget file. Bone mapping index out of range.");
            }
        }

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
        header.SourceBoneCount = count;
        PreRotations.SetSize(count);
        PostRotations.SetSize(count);
        RetargetedBindPose.SetSize(count);
        RetargetedInversePose.SetSize(count);
        ModelBoneIdToAnimationBoneId.SetSize(count);
		
		int i = 0;
		for (auto & id : ModelBoneIdToAnimationBoneId)
		{
			id = -1;
			i++;
		}
	}

	// used in Rendering
	void Pose::GetMatrices(const Skeleton * skeleton, CoreLib::List<VectorMath::Matrix4>& matrices, bool multiplyInversePose, RetargetFile * retarget) const
	{
		matrices.Clear();
		matrices.SetSize(skeleton->Bones.Count());
        struct BoneRotationOverride
        {
            BoneTransformation transform;
            float weight;
        };
        CoreLib::List<CoreLib::List<BoneRotationOverride>> morphStateOverrides;
        if (retarget && retarget->MorphStates.Count())
        {
            morphStateOverrides.SetSize(matrices.Count());
            for (auto &weight : BlendShapeWeights)
            {
                if (weight.Value == 0.0f)
                    continue;
                int msId = retarget->MorphStates.FindFirst([&](auto &ms) { return ms.Name == weight.Key; });
                if (msId != -1)
                {
                    for (auto bs : retarget->MorphStates[msId].BoneStates)
                    {
                        morphStateOverrides[bs.BoneId].Add(BoneRotationOverride{bs.Transform, weight.Value});
                    }
                }
            }
        }

		// Consider the case that the skeleton for animation may contain less bones than the skeleton for mesh
		// So, we should skip those missing bones and using its bind pose
		for (int i = 0; i < matrices.Count(); i++)
		{
            if (retarget)
            {
                matrices[i] = retarget->RetargetedBindPose[i].ToMatrix();
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
					transform.Translation = retarget->RetargetedBindPose[i].Translation;
                auto rotation = retarget->PreRotations[i] * transform.Rotation;
                if (morphStateOverrides.Count() && morphStateOverrides[i].Count())
                {
                    for (auto &bs : morphStateOverrides[i])
                    {
                        rotation = VectorMath::Quaternion::Slerp(rotation, bs.transform.Rotation, bs.weight * 0.01f);
                        transform.Translation =
                            VectorMath::Vec3::Lerp(transform.Translation, bs.transform.Translation, bs.weight * 0.01f);
                    }
                }
                matrices[i] = (rotation * retarget->PostRotations[i]).ToMatrix4();
                matrices[i].SetTranslation(transform.Translation);
			}
			else
			{
				transform = Transforms[i];
				if (i != 0)
					transform.Translation = skeleton->Bones[i].BindPose.Translation;
                matrices[i] = transform.ToMatrix();
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
