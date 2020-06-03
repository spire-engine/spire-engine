#ifndef TEST_USER_ACTOR_H
#define TEST_USER_ACTOR_H

#include "MeshBuilder.h"
#include "CoreLib/WinForm/WinApp.h"
#include "CoreLib/WinForm/WinForm.h"
#include "CoreLib/WinForm/WinCommonDlg.h"
#include "Engine.h"
#include "Level.h"
#include "RendererService.h"
#include "CoreLib/LibUI/LibUI.h"
#include "OS.h"
#include "AnimationSynthesizer.h"
#include "ArcBallCameraController.h"
#include "CameraActor.h"

using namespace GraphicsUI;
using namespace GameEngine;

enum class EditorMode
{
	EditPreRotation, EditMorphState, EditMapping
};

class SkeletonRetargetVisualizerActor : public Actor
{
private:
	RefPtr<ModelPhysicsInstance> physInstance, skeletonPhysInstance, targetSkeletonPhysInstance;
	RefPtr<Drawable> targetSkeletonDrawable, sourceSkeletonDrawable;
	RefPtr<Drawable> xAxisDrawable, yAxisDrawable, zAxisDrawable;
	RefPtr<SystemWindow> sysWindow;
	Mesh * mMesh = nullptr;
	Skeleton * mSkeleton = nullptr;
    EditorMode editorMode = EditorMode::EditPreRotation;
    int editingMorphStateId;
	Matrix4 targetSkeletonTransform, oldTargetSkeletonTransform;
	Mesh sourceSkeletonMesh, targetSkeletonMesh;
	Quaternion originalTransform;
    Vec3 originalTranslation;
	RefPtr<Model> sourceModel, sourceSkeletonModel, targetSkeletonModel;
	TransformManipulator * manipulator;
	ModelDrawableInstance sourceModelInstance, highlightModelInstance;
	RefPtr<Skeleton> targetSkeleton;
	Material sourceSkeletonMaterial, targetSkeletonMaterial, sourceMeshMaterial,
		xAxisMaterial, yAxisMaterial, zAxisMaterial;
	ManipulationMode manipulationMode = ManipulationMode::Translation;
    ManipulationMode morphStateManipulationMode = ManipulationMode::Translation;

	GraphicsUI::Label * lblFrame, * lblEditorMode;
	GraphicsUI::Form * infoForm;
	GraphicsUI::MultiLineTextBox * infoFormTextBox;
	GraphicsUI::ListBox* lstBones;
	GraphicsUI::TextBox *txtX, *txtY, *txtZ;
	GraphicsUI::TextBox *txtScaleX, *txtScaleY, *txtScaleZ;
	GraphicsUI::MultiLineTextBox * txtInfo;
	List<GraphicsUI::ComboBox*> cmbAnimBones;
    GraphicsUI::VScrollPanel *pnlBoneMapping;
	GraphicsUI::Container *pnlMorphState;
	GraphicsUI::ScrollBar * scTimeline;
    GraphicsUI::ListBox *lstMorphStates;
    GraphicsUI::Button *btnEditMorphState;
	RetargetFile retargetFile;
	SkeletalAnimation currentAnim;
	GraphicsUI::UIEntry * uiEntry = nullptr;
	bool isPlaying = false;
	bool showSourceModel = true; // if false, draw skeleton
	bool targetSkeletonSelected = false;
	float curTime = 0.0f;
	RefPtr<SimpleAnimationSynthesizer> animSynthesizer = new SimpleAnimationSynthesizer();
	List<Vec3> bonePositions;
	struct Modification
	{
		int boneId;
        String morphStateName;
        Vec3 originalTranslation, newTranslation;
		Quaternion originalTransform, newTransform;
        EditorMode editorMode;
	};
	List<Modification> undoStack;
public:
	void ToggleMappingMode(bool v)
	{
        if (v)
            editorMode = EditorMode::EditMapping;
        else
            editorMode = EditorMode::EditPreRotation;
        UpdateEditorMode();
	}

    void UpdateEditorMode()
    {
        if (editorMode == EditorMode::EditMapping)
        {
            this->targetSkeletonSelected = false;
            pnlBoneMapping->BackColor = GraphicsUI::Color(0xBF, 0x36, 0x0C, 200);
			lblEditorMode->SetText("Editing Bone Mapping");
        }
        else
        {
            pnlBoneMapping->BackColor = GraphicsUI::Color(0, 0, 0, 0);
        }
        if (editorMode == EditorMode::EditMorphState)
        {
            lblEditorMode->SetText("Editing Bone Mapping");
            lblEditorMode->SetText(
                "Editing Morph State '" + lstMorphStates->GetTextItem(editingMorphStateId)->GetText() + "'");
            lstMorphStates->Enabled = false;
            pnlMorphState->BackColor = GraphicsUI::Color(0xBF, 0x36, 0x0C, 200);
        }
        else
        {
            lstMorphStates->Enabled = true;
            pnlMorphState->BackColor = GraphicsUI::Color(0, 0, 0, 0);
        }
        lblEditorMode->Visible = editorMode != EditorMode::EditPreRotation;
    }
	virtual EngineActorType GetEngineType() override
	{
		return EngineActorType::Drawable;
	}

	virtual void Tick() override
	{
        if (!scTimeline)
            return;
		if (isPlaying)
		{
			if (currentAnim.Duration > 0.0f)
			{
				auto deltaTime = Engine::Instance()->GetTimeDelta(EngineThread::Rendering);
				curTime += deltaTime;
				auto time = fmod(curTime, currentAnim.Duration);
				int newFrame = (int)(curTime * 30.0f);
				while (newFrame >= scTimeline->GetMax())
					newFrame -= scTimeline->GetMax();
				if (newFrame < 1)
					newFrame = 1;
				if (newFrame < scTimeline->GetMax())
					scTimeline->SetPosition(newFrame);
			}
		}
		else
			curTime = scTimeline->GetPosition() / 30.0f;

		ManipulatorSceneView view;
		view.FOV = level->CurrentCamera->GetView().FOV;
		auto viewport = Engine::Instance()->GetCurrentViewport();
		view.ViewportX = (float)viewport.x;
		view.ViewportY = (float)viewport.y;
		view.ViewportW = (float)viewport.width;
		view.ViewportH = (float)viewport.height;

		if (sourceModel && lstBones->SelectedIndex != -1)
		{
			auto sourceSkeleton = sourceModel->GetSkeleton();
			bonePositions.SetSize(sourceSkeleton->Bones.Count());
			if (curPose.Transforms.Count())
			{
				List<Matrix4> matrices;
				curPose.GetMatrices(sourceSkeleton, matrices, false, &retargetFile);
				for (int i = 0; i < sourceSkeleton->Bones.Count(); i++)
				{
					bonePositions[i] = Vec3::Create(matrices[i].values[12], matrices[i].values[13], matrices[i].values[14]);
				}
			}
            auto mode = ManipulationMode::Rotation;
            if (editorMode == EditorMode::EditMorphState)
                mode = morphStateManipulationMode;
            manipulator->SetTarget(mode, view, level->CurrentCamera->GetCameraTransform(),
                level->CurrentCamera->GetPosition(), bonePositions[lstBones->SelectedIndex]);
			manipulator->Visible = true;
		}
		else if (targetSkeletonSelected)
		{
			manipulator->SetTarget(manipulationMode, view, level->CurrentCamera->GetCameraTransform(), level->CurrentCamera->GetPosition(),
				Vec3::Create(targetSkeletonTransform.values[12], targetSkeletonTransform.values[13], targetSkeletonTransform.values[14]));
			manipulator->Visible = true;
		}
		else
			manipulator->Visible = false;
		if (physInstance)
		{
			Bounds.Init();
			for (auto & obj : physInstance->objects)
				Bounds.Union(obj->GetBounds());
			auto boundExtent = (Bounds.Max - Bounds.Min);
			Bounds.Min -= boundExtent;
			Bounds.Max += boundExtent;
		}
	}

	void SetBoneMapping(int sourceBone, int animBone)
	{
		retargetFile.ModelBoneIdToAnimationBoneId[sourceBone] = animBone;
        auto sourceSkeleton = sourceModel->GetSkeleton();
		UpdateCombinedRetargetTransform();
	}

	void UpdateCombinedRetargetTransform()
	{
        auto sourceSkeleton = sourceModel->GetSkeleton();
        List<Matrix4> forwardGlobalMatrix;
        List<Matrix4> retargetGlobalBindPoseTransform;
        List<Quaternion> targetBindPoseRotations;
        List<Quaternion> retargetBindPoseRotations;
        List<Quaternion> retargetGlobalBindPoseRotations;
        if (targetSkeleton)
        {
            targetBindPoseRotations.SetSize(targetSkeleton->Bones.Count());
            for (int i = 0; i < targetSkeleton->Bones.Count(); i++)
            {
                targetBindPoseRotations[i] = targetSkeleton->Bones[i].BindPose.Rotation;
                int parentId = targetSkeleton->Bones[i].ParentId;
                if (parentId != -1)
                {
                    targetBindPoseRotations[i] =
                        targetBindPoseRotations[parentId] * targetBindPoseRotations[i];
                }
            }
        }
        forwardGlobalMatrix.SetSize(sourceSkeleton->Bones.Count());
        retargetBindPoseRotations.SetSize(sourceSkeleton->Bones.Count());
        retargetGlobalBindPoseRotations.SetSize(sourceSkeleton->Bones.Count());
        retargetGlobalBindPoseTransform.SetSize(sourceSkeleton->Bones.Count());

        for (int i = 0; i < sourceSkeleton->Bones.Count(); i++)
        {
            int parentId = sourceSkeleton->Bones[i].ParentId;
            int targetId = retargetFile.ModelBoneIdToAnimationBoneId[i];
            retargetFile.PostRotations[i] = Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
            forwardGlobalMatrix[i] = sourceSkeleton->Bones[i].BindPose.ToMatrix();
            if (targetId != -1)
            {
                // We need to find a retargeted bind pose rotation such that the global
				// coordinate space of the retargeted bind pose at this node is the same as the target
				// skeleton node.
                auto targetGlobalSpace = targetBindPoseRotations[targetId];
                Quaternion parentGlobalRotation = Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
                if (parentId != -1)
                    parentGlobalRotation = retargetGlobalBindPoseRotations[parentId];
				retargetBindPoseRotations[i] = parentGlobalRotation.Inverse() * targetGlobalSpace;
                retargetFile.PostRotations[i] =
                    targetSkeleton->Bones[targetId].BindPose.Rotation.Inverse() * retargetBindPoseRotations[i]; 
            }
            else
            {
				// If there is no matching target bone, just use world-space aligned local coordinates.
                retargetBindPoseRotations[i] = Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
                retargetFile.PostRotations[i] = Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
            }
			// Compute global bind pose rotations and matrices.
            retargetGlobalBindPoseRotations[i] = retargetBindPoseRotations[i];
            if (parentId != -1)
            {
                forwardGlobalMatrix[i] = forwardGlobalMatrix[parentId] * forwardGlobalMatrix[i];
                retargetGlobalBindPoseRotations[i] = retargetGlobalBindPoseRotations[parentId] * retargetBindPoseRotations[i];
            }
        }

		// Figure out the correct translation terms.
        for (int i = 0; i < sourceSkeleton->Bones.Count(); i++)
        {
            int parentId = sourceSkeleton->Bones[i].ParentId;
            Vec3 targetGlobalTranslation = forwardGlobalMatrix[i].GetTranslation();
            Matrix4 parentGlobalBindPoseTransform;
			if (parentId == -1)
                Matrix4::CreateIdentityMatrix(parentGlobalBindPoseTransform);
            else
            {
                parentGlobalBindPoseTransform = retargetGlobalBindPoseTransform[parentId];
            }
            Matrix4 invParentGlobalBindPoseTransform;
            parentGlobalBindPoseTransform.Inverse(invParentGlobalBindPoseTransform);
            Vec3 localTranslation = invParentGlobalBindPoseTransform.Transform(Vec4::Create(targetGlobalTranslation, 1.0f)).xyz();
            retargetFile.RetargetedBindPose[i].Translation = localTranslation;
            retargetFile.RetargetedBindPose[i].Rotation = retargetBindPoseRotations[i];
            Matrix4 retargetBindPoseTransform = retargetBindPoseRotations[i].ToMatrix4();
            retargetBindPoseTransform.SetTranslation(localTranslation);
            retargetGlobalBindPoseTransform[i] = parentGlobalBindPoseTransform * retargetBindPoseTransform;
            retargetGlobalBindPoseTransform[i].Inverse(retargetFile.RetargetedInversePose[i]);
            
        }
	}

	void UpdateMorphStateView()
    {
        lstMorphStates->Clear();
        for (auto &state : retargetFile.MorphStates)
        {
            lstMorphStates->AddTextItem(state.Name);
        }
        for (auto &channel : currentAnim.BlendShapeChannels)
        {
            if (retargetFile.MorphStates.FindFirst([&](auto &ms) { return ms.Name == channel.Name; }) == -1)
            {
                int item = lstMorphStates->AddTextItem(channel.Name);
                lstMorphStates->GetTextItem(item)->FontColor = GraphicsUI::Global::Colors.MenuItemDisabledForeColor; 
            }
        }
    }

	void LoadTargetSkeleton(GraphicsUI::UI_Base *)
	{
		if (!sourceModel)
		{
			Engine::Instance()->GetMainWindow()->ShowMessage("Please load source model first.", "Error");
			return;
		}
		RefPtr<GameEngine::OsFileDialog> dlg = OsApplication::CreateFileDialog(Engine::Instance()->GetMainWindow());
		dlg->Filter = "Skeleton|*.skeleton|All Files|*.*";
		if (dlg->ShowOpen())
		{
			Engine::Instance()->GetRenderer()->Wait();
			Matrix4::Translation(targetSkeletonTransform, 200.0f, 0.0f, 0.0f);
			targetSkeleton = new Skeleton();
			targetSkeleton->LoadFromFile(dlg->FileName);
			targetSkeletonMesh.FromSkeleton(targetSkeleton.Ptr(), 6.0f);
			targetSkeletonDrawable = nullptr;
			targetSkeletonMaterial.SetVariable("highlightId", -1);
			retargetFile.TargetSkeletonName = targetSkeleton->Name;
			for (auto & id : retargetFile.ModelBoneIdToAnimationBoneId)
				id = -1;
			int j = 0;
			int i = 0;
			for (auto & b : sourceModel->GetSkeleton()->Bones)
			{
				j = 0;
				for (auto & ab : targetSkeleton->Bones)
				{
					if (b.Name == ab.Name)
						retargetFile.ModelBoneIdToAnimationBoneId[i] = j;
					j++;
				}
				i++;
			}
			UpdateBoneMappingPanel();
			currentAnim = SkeletalAnimation();
			targetSkeletonModel = new Model(&targetSkeletonMesh, targetSkeleton.Ptr(), &targetSkeletonMaterial);
			targetSkeletonPhysInstance = targetSkeletonModel->CreatePhysicsInstance(level->GetPhysicsScene(), this, (void*)2);
            retargetFile.header.TargetBoneCount = targetSkeleton->Bones.Count();
		}
		undoStack.Clear();
		undoPtr = -1;
	}
	
	void mnToggleSkeletonModel_Clicked(UI_Base *)
	{
		showSourceModel = !showSourceModel;
	}

	void ViewBindPose(GraphicsUI::UI_Base *)
	{
		scTimeline->SetPosition(0);
	}

	void LoadAnimation(GraphicsUI::UI_Base *)
	{
		RefPtr<GameEngine::OsFileDialog> dlg = OsApplication::CreateFileDialog(Engine::Instance()->GetMainWindow());
		dlg->Filter = "Animation|*.anim|All Files|*.*";
		if (dlg->ShowOpen())
		{
			currentAnim.LoadFromFile(dlg->FileName);
            curPose.Transforms.Clear();
			scTimeline->SetValue(0, (int)(currentAnim.Duration * 30.0f), 0, 1);
            UpdateMorphStateView();
		}
	}

	void LoadSourceModel(GraphicsUI::UI_Base *)
	{
        RefPtr<GameEngine::OsFileDialog> dlg = OsApplication::CreateFileDialog(Engine::Instance()->GetMainWindow());
		dlg->Filter = "Model|*.model|All Files|*.*";
		if (dlg->ShowOpen())
		{
			Engine::Instance()->GetRenderer()->Wait();

			sourceModel = new Model();
			sourceModel->LoadFromFile(level, dlg->FileName);
			sourceModelInstance.Drawables.Clear();
			sourceSkeletonMesh.FromSkeleton(sourceModel->GetSkeleton(), 5.0f);
			sourceSkeletonDrawable = nullptr;
			sourceSkeletonMaterial.SetVariable("highlightId", -1);
			sourceMeshMaterial.SetVariable("highlightId", -1);
			sourceSkeletonModel = new Model(&sourceSkeletonMesh, sourceModel->GetSkeleton(), &sourceSkeletonMaterial);
			highlightModelInstance.Drawables = List<RefPtr<Drawable>>();
			retargetFile.SourceSkeletonName = sourceModel->GetSkeleton()->Name;
			retargetFile.SetBoneCount(sourceModel->GetSkeleton()->Bones.Count());
			targetSkeleton = nullptr;
			targetSkeletonModel = nullptr;
			targetSkeletonPhysInstance = nullptr;
			targetSkeletonDrawable = nullptr;
			lstBones->Clear();
			auto sourceSkeleton = sourceModel->GetSkeleton();
			for (int i = 0; i < sourceSkeleton->Bones.Count(); i++)
				lstBones->AddTextItem(sourceSkeleton->Bones[i].Name);
			physInstance = sourceModel->CreatePhysicsInstance(level->GetPhysicsScene(), this, 0);
			skeletonPhysInstance = sourceSkeletonModel->CreatePhysicsInstance(level->GetPhysicsScene(), this, (void*)1);
            curPose.Transforms.SetSize(sourceSkeleton->Bones.Count());
            for (int i = 0; i < sourceSkeleton->Bones.Count(); i++)
            {
                curPose.Transforms[i] = sourceSkeleton->Bones[i].BindPose;
                retargetFile.PreRotations[i] = Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
            }
			UpdateCombinedRetargetTransform();
		}
		undoStack.Clear();
		undoPtr = -1;
	}

	void Save(GraphicsUI::UI_Base *)
	{
        RefPtr<GameEngine::OsFileDialog> dlg = OsApplication::CreateFileDialog(Engine::Instance()->GetMainWindow());
		dlg->Filter = "Retarget File|*.retarget|All Files|*.*";
		dlg->DefaultEXT = "retarget";
		if (dlg->ShowSave())
		{
			retargetFile.SaveToFile(dlg->FileName);
		}
	}

	void Open(GraphicsUI::UI_Base *)
	{
        RefPtr<GameEngine::OsFileDialog> dlg = OsApplication::CreateFileDialog(Engine::Instance()->GetMainWindow());
		dlg->Filter = "Retarget File|*.retarget|All Files|*.*";
		dlg->DefaultEXT = "retarget";
		if (sourceModel && targetSkeleton)
		{
			if (dlg->ShowOpen())
			{
				RetargetFile file;
				file.LoadFromFile(dlg->FileName);
				bool isValid = true;
				for (auto id : file.ModelBoneIdToAnimationBoneId)
					if (id >= targetSkeleton->Bones.Count())
						isValid = false;
				if (!isValid || file.ModelBoneIdToAnimationBoneId.Count() != sourceModel->GetSkeleton()->Bones.Count())
					Engine::Instance()->GetMainWindow()->ShowMessage("This retarget file does not match the current model / target skeleton.", "Error");
				else
				{
					retargetFile = file;
					txtScaleX->SetText(String(file.RootTranslationScale.x));
					txtScaleY->SetText(String(file.RootTranslationScale.y));
					txtScaleZ->SetText(String(file.RootTranslationScale.z));
					UpdateMappingSelection();
					UpdateCombinedRetargetTransform();
                    UpdateMorphStateView();
				}
			}
		}
		else
			Engine::Instance()->GetMainWindow()->ShowMessage("Please load source model and target skeleton first.", "Error");
	}
	
	bool disableTextChange = false;

	int GetAnimationBoneIndex(int modelBoneIndex)
	{
		if (modelBoneIndex == -1)
			return -1;
		if (retargetFile.ModelBoneIdToAnimationBoneId.Count() <= modelBoneIndex)
			return modelBoneIndex;
		return retargetFile.ModelBoneIdToAnimationBoneId[modelBoneIndex];
	}

	void ChangeScaleX(GraphicsUI::UI_Base * ctrl)
	{
		if (disableTextChange)
			return;
		try
		{
			auto txt = dynamic_cast<GraphicsUI::TextBox*>(ctrl);
			CoreLib::Text::TokenReader p(txt->GetText());

			float val = p.ReadFloat();
			retargetFile.RootTranslationScale.x = val;
		}
		catch (...)
		{
		}
	}

	void ChangeScaleY(GraphicsUI::UI_Base * ctrl)
	{
		if (disableTextChange)
			return;
		try
		{
			auto txt = dynamic_cast<GraphicsUI::TextBox*>(ctrl);
			CoreLib::Text::TokenReader p(txt->GetText());

			float val = p.ReadFloat();
			retargetFile.RootTranslationScale.y = val;

		}
		catch (...)
		{
		}
	}
	
	void ChangeScaleZ(GraphicsUI::UI_Base * ctrl)
	{
		if (disableTextChange)
			return;
		try
		{
			auto txt = dynamic_cast<GraphicsUI::TextBox*>(ctrl);
			CoreLib::Text::TokenReader p(txt->GetText());

			float val = p.ReadFloat();
			retargetFile.RootTranslationScale.z = val;

		}
		catch (...)
		{
		}
	}

	void ChangeRotation(GraphicsUI::UI_Base * ctrl)
	{
		if (disableTextChange)
			return;
		if (lstBones->SelectedIndex != -1)
		{
			try
			{
				auto txt = dynamic_cast<GraphicsUI::TextBox*>(ctrl);
				CoreLib::Text::TokenReader p(txt->GetText());

				float angle = p.ReadFloat();
				if (angle > -360.0f && angle < 360.0f)
				{
					auto & info = retargetFile.PostRotations[lstBones->SelectedIndex];
					EulerAngleToQuaternion(info, StringToFloat(txtX->GetText()) / 180.0f * Math::Pi, StringToFloat(txtY->GetText()) / 180.0f * Math::Pi, StringToFloat(txtZ->GetText()) / 180.0f * Math::Pi, EulerAngleOrder::ZXY);
				}
				UpdateCombinedRetargetTransform();

			}
			catch (...)
			{
			}
		}
	}

	void UpdateMappingSelection()
	{
		if (cmbAnimBones.Count() == retargetFile.ModelBoneIdToAnimationBoneId.Count())
		{
			for (int i = 0; i < retargetFile.ModelBoneIdToAnimationBoneId.Count(); i++)
				cmbAnimBones[i]->SetSelectedIndex(Math::Clamp(retargetFile.ModelBoneIdToAnimationBoneId[i] + 1, -1, cmbAnimBones[i]->Items.Count() - 1));
		}
	}

	void SelectedBoneChanged(GraphicsUI::UI_Base *)
	{
		sourceMeshMaterial.SetVariable("highlightId", lstBones->SelectedIndex);
		sourceSkeletonMaterial.SetVariable("highlightId", lstBones->SelectedIndex);
		targetSkeletonMaterial.SetVariable("highlightId", GetAnimationBoneIndex(lstBones->SelectedIndex));
		if (lstBones->SelectedIndex != -1)
		{
			float x, y, z;
			QuaternionToEulerAngle(retargetFile.PostRotations[lstBones->SelectedIndex], x, y, z, EulerAngleOrder::ZXY);
			disableTextChange = true;
			txtX->SetText(String(x * 180.0f / Math::Pi, "%.1f"));
			txtY->SetText(String(y * 180.0f / Math::Pi, "%.1f"));
			txtZ->SetText(String(z * 180.0f / Math::Pi, "%.1f"));
		
			StringBuilder sbInfo;
			auto sourceSkeleton = sourceModel->GetSkeleton();
			sbInfo << sourceSkeleton->Bones[lstBones->SelectedIndex].Name << "\n";
			sbInfo << "Parent: ";
			if (sourceSkeleton->Bones[lstBones->SelectedIndex].ParentId == -1)
				sbInfo << "null\n";
			else
				sbInfo << sourceSkeleton->Bones[sourceSkeleton->Bones[lstBones->SelectedIndex].ParentId].Name << "\n";

			QuaternionToEulerAngle(sourceSkeleton->Bones[lstBones->SelectedIndex].BindPose.Rotation, x, y, z, EulerAngleOrder::ZXY);
			sbInfo << "BindPose:\n  Rotation " << String(x* 180.0f / Math::Pi, "%.1f") << ", " << String(y* 180.0f / Math::Pi, "%.1f") << ", " << String(z* 180.0f / Math::Pi, "%.1f") << "\n";
			auto trans = sourceSkeleton->Bones[lstBones->SelectedIndex].BindPose.Translation;
			sbInfo << "  Offset " << String(trans.x, "%.1f") << ", " << String(trans.y, "%.1f") << ", " << String(trans.z, "%.1f") << "\n";
			int animBoneId = GetAnimationBoneIndex(lstBones->SelectedIndex);
			if (animBoneId != -1 && targetSkeleton)
			{
				sbInfo << "AnimPose: " << targetSkeleton->Bones[animBoneId].Name << "\n";
				QuaternionToEulerAngle(targetSkeleton->Bones[animBoneId].BindPose.Rotation, x, y, z, EulerAngleOrder::ZXY);
				sbInfo << "  Rotation " << String(x* 180.0f / Math::Pi, "%.1f") << ", " << String(y* 180.0f / Math::Pi, "%.1f") << ", " << String(z* 180.0f / Math::Pi, "%.1f") << "\n";
				auto trans = targetSkeleton->Bones[animBoneId].BindPose.Translation;
				sbInfo << "  Offset " << String(trans.x, "%.1f") << ", " << String(trans.y, "%.1f") << ", " << String(trans.z, "%.1f") << "\n";
			}
            switch (editorMode)
            {
            case EditorMode::EditPreRotation:
				{
					originalTransform = retargetFile.PreRotations[lstBones->SelectedIndex];
					break;
				}
            case EditorMode::EditMorphState:
				{
					auto boneStateId = retargetFile.MorphStates[editingMorphStateId].BoneStates.FindFirst(
						[&](auto &bs) { return bs.BoneId == lstBones->SelectedIndex; });
					if (boneStateId == -1)
					{
						originalTransform = Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
                        originalTranslation = retargetFile.RetargetedBindPose[lstBones->SelectedIndex].Translation;
					}
					else
					{
						originalTransform =
							retargetFile.MorphStates[editingMorphStateId].BoneStates[boneStateId].Transform.Rotation;
                        originalTranslation =
                            retargetFile.MorphStates[editingMorphStateId].BoneStates[boneStateId].Transform.Translation;
					}
					break;
				}
            }
			txtInfo->SetText(sbInfo.ProduceString());
			disableTextChange = false;
		}
	}

	void cmbBoneMappingChanged(UI_Base * sender)
	{
		int idx = -1;
		for (int i = 0; i < cmbAnimBones.Count(); i++)
		{
			if (cmbAnimBones[i] == sender)
			{
				idx = i;
				break;
			}
		}
		if (idx != -1 && targetSkeleton)
		{
			SetBoneMapping(idx, Math::Clamp(cmbAnimBones[idx]->SelectedIndex - 1, -1, targetSkeleton->Bones.Count() - 1));
			targetSkeletonMaterial.SetVariable("highlightId", retargetFile.ModelBoneIdToAnimationBoneId[idx]);
		}
	}

	Pose GetNullPose()
    {
        Pose rs;
        if (targetSkeleton)
        {
            rs.Transforms.SetSize(targetSkeleton->Bones.Count());
            for (int i = 0; i < targetSkeleton->Bones.Count(); i++)
                rs.Transforms[i] = retargetFile.RetargetedBindPose[i];
        }
        else if (auto srcSkeleton = sourceModel->GetSkeleton())
        {
            rs.Transforms.SetSize(srcSkeleton->Bones.Count());
            for (int i = 0; i < srcSkeleton->Bones.Count(); i++)
                rs.Transforms[i] = BoneTransformation();
        }

        return rs;
    }

	Pose GetAnimSkeletonBindPose()
	{
		Pose rs;
		if (targetSkeleton)
		{
			rs.Transforms.SetSize(targetSkeleton->Bones.Count());
			for (int i = 0; i < targetSkeleton->Bones.Count(); i++)
				rs.Transforms[i] = targetSkeleton->Bones[i].BindPose;
		}
		return rs;
	}

	Pose GetSourceSkeletonBindPose()
    {
        Pose rs;
        if (auto srcSkeleton = sourceModel->GetSkeleton())
        {
            rs.Transforms.SetSize(srcSkeleton->Bones.Count());
            for (int i = 0; i < srcSkeleton->Bones.Count(); i++)
                rs.Transforms[i] = srcSkeleton->Bones[i].BindPose;
        }
        return rs;
    }

	Pose GetCurrentPose()
	{
        Pose result;
        if (targetSkeleton)
        {
            if (scTimeline->GetPosition() == 0)
                result = GetAnimSkeletonBindPose();
            else
            {
                animSynthesizer->SetSource(targetSkeleton.Ptr(), &currentAnim);
                float time = scTimeline->GetPosition() / 30.0f;
                animSynthesizer->GetPose(result, time);
            }
        }
        else
            result = GetNullPose();
        if (editorMode == EditorMode::EditMorphState)
        {
            result.BlendShapeWeights[retargetFile.MorphStates[editingMorphStateId].Name] = 100.0f;
        }
        return result;
	}

	void GetSkeletonInfo(StringBuilder & sb, Skeleton * skeleton, int id, int indent)
	{
		for (int i = 0; i < indent; i++)
			sb << "  ";
		sb << "\"" << skeleton->Bones[id].Name << "\"\n"; 
		for (int i = 0; i < indent; i++)
			sb << "  ";
		sb << "{\n";
		for (int i = 0; i < skeleton->Bones.Count(); i++)
		{
			if (skeleton->Bones[i].ParentId == id)
				GetSkeletonInfo(sb, skeleton, i, indent + 1);
		}
		for (int i = 0; i < indent; i++)
			sb << "  ";
		sb << "}\n";
		
	}
	void mnTranslationTransformMode_Clicked(UI_Base * sender)
	{
		manipulationMode = ManipulationMode::Translation;
	}
	void mnScaleTransformMode_Clicked(UI_Base * sender)
	{
		manipulationMode = ManipulationMode::Scale;
	}
	void mnMappingMode_Clicked(UI_Base * sender)
	{
        ToggleMappingMode(editorMode != EditorMode::EditMapping);
	}
	void mnPlayAnim_Clicked(UI_Base * sender)
	{
		isPlaying = !isPlaying;
	}

	void mnShowTargetSkeletonShape_Clicked(UI_Base * sender)
	{
		if (targetSkeleton)
		{
			StringBuilder sb;
			GetSkeletonInfo(sb, targetSkeleton.Ptr(), 0, 0);
			infoFormTextBox->SetText(sb.ProduceString());
			uiEntry->ShowWindow(infoForm);
		}
	}

	void mnShowSourceSkeletonShape_Clicked(UI_Base * sender)
	{
		if (sourceModel)
		{
			StringBuilder sb;
			GetSkeletonInfo(sb, sourceModel->GetSkeleton(), 0, 0);
			infoFormTextBox->SetText(sb.ProduceString());
			uiEntry->ShowWindow(infoForm);
		}
	}

	void scTimeline_Changed(UI_Base * sender)
	{
		if (scTimeline->GetPosition() == 0)
			lblFrame->SetText("BindPose");
		else
			lblFrame->SetText(String("Animation: ") + String(scTimeline->GetPosition() / 30.0f, "%.2f") + "s");
	}

	void UpdateBoneMappingPanel()
	{
		cmbAnimBones.Clear();
		pnlBoneMapping->ClearChildren();
		for (int i = 0; i < sourceModel->GetSkeleton()->Bones.Count(); i++)
		{
			auto name = sourceModel->GetSkeleton()->Bones[i].Name;
			auto lbl = new GraphicsUI::Label(pnlBoneMapping);
			lbl->SetText(name);
			lbl->Posit(EM(0.2f), EM(i * 2.5f), EM(12.5f), EM(1.0f));
			int selIdx = -1;
			auto cmb = new GraphicsUI::ComboBox(pnlBoneMapping);
			selIdx = retargetFile.ModelBoneIdToAnimationBoneId[i];
			cmb->AddTextItem("(None)");
			for (auto & b : targetSkeleton->Bones)
				cmb->AddTextItem(b.Name);
			cmb->SetSelectedIndex(selIdx + 1);
			cmbAnimBones.Add(cmb);
			cmb->Posit(EM(0.2f), EM(i * 2.5f + 1.1f), EM(11.5f), EM(1.2f));
			cmb->OnChanged.Bind(this, &SkeletonRetargetVisualizerActor::cmbBoneMappingChanged);
		}
		pnlBoneMapping->SizeChanged();
		UpdateCombinedRetargetTransform();
	}

	void GetAccumRotation(int id, Quaternion &qLocal, Quaternion &q, Quaternion &qParent)
	{
        List<Matrix4> matrices;
        auto pose = GetCurrentPose();
        
		pose.GetMatrices(sourceModel->GetSkeleton(), matrices, false, &retargetFile);
        qLocal = Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
        int targetId = retargetFile.ModelBoneIdToAnimationBoneId[id];
        if (targetId != -1)
            qLocal = pose.Transforms[targetId].Rotation;
        q = Quaternion::FromMatrix(matrices[id].GetMatrix3());
        int parentId = sourceModel->GetSkeleton()->Bones[id].ParentId;
        if (parentId != -1)
            qParent = Quaternion::FromMatrix(matrices[parentId].GetMatrix3());
        else
            qParent = Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
	}

	void GetAccumTransform(int id, Matrix4 &t, Matrix4 &tParent)
    {
        List<Matrix4> matrices;
        GetCurrentPose().GetMatrices(sourceModel->GetSkeleton(), matrices, false, &retargetFile);
        t = matrices[id];
        int parentId = sourceModel->GetSkeleton()->Bones[id].ParentId;
        if (parentId != -1)
            tParent = matrices[parentId];
        else
            Matrix4::CreateIdentityMatrix(tParent);
    }

	void ApplyManipulation(UI_Base *, ManipulationEventArgs e)
	{
        if (editorMode == EditorMode::EditMapping)
            return;
		PreviewManipulation(nullptr, e);
		if (IsRotationHandle(e.Handle))
		{
			Modification m;
            m.editorMode = editorMode;
			m.boneId = lstBones->SelectedIndex;
			m.originalTransform = originalTransform;
            m.newTranslation = m.originalTranslation = originalTranslation;
            if (editorMode == EditorMode::EditPreRotation)
            {
				m.newTransform = retargetFile.PreRotations[lstBones->SelectedIndex];
				originalTransform = retargetFile.PreRotations[lstBones->SelectedIndex];
            }
            else if (editorMode == EditorMode::EditMorphState)
            {
                m.morphStateName = retargetFile.MorphStates[editingMorphStateId].Name;
                int bsId = retargetFile.MorphStates[editingMorphStateId].BoneStates.FindFirst(
                    [&](auto &bs) { return bs.BoneId == lstBones->SelectedIndex; });
                originalTransform = m.newTransform =
                    retargetFile.MorphStates[editingMorphStateId].BoneStates[bsId].Transform.Rotation;
            }
			undoStack.SetSize(undoPtr + 1);
			undoStack.Add(m);
			undoPtr = undoStack.Count() - 1;
			UpdateCombinedRetargetTransform();
		}
        else
        {
            if (editorMode == EditorMode::EditMorphState && IsTranslationHandle(e.Handle))
            {
                Modification m;
                m.editorMode = editorMode;
                m.boneId = lstBones->SelectedIndex;
                m.newTransform = originalTransform;
                m.originalTransform = originalTransform;
                m.originalTranslation = originalTranslation;
                m.morphStateName = retargetFile.MorphStates[editingMorphStateId].Name;
                int bsId = retargetFile.MorphStates[editingMorphStateId].BoneStates.FindFirst(
                    [&](auto &bs) { return bs.BoneId == lstBones->SelectedIndex; });
                originalTranslation = m.newTranslation =
                    retargetFile.MorphStates[editingMorphStateId].BoneStates[bsId].Transform.Translation;
                undoStack.SetSize(undoPtr + 1);
                undoStack.Add(m);
                undoPtr = undoStack.Count() - 1;
            }
            else
            {
				oldTargetSkeletonTransform = targetSkeletonTransform;
            }
        }
	}

	void PreviewManipulation(UI_Base *, ManipulationEventArgs e)
	{
        if (editorMode == EditorMode::EditMapping)
            return;
		if (IsTranslationHandle(e.Handle))
		{
            if (editorMode == EditorMode::EditMorphState)
            {
                int bsId = retargetFile.MorphStates[editingMorphStateId].BoneStates.FindFirst(
                    [&](auto &bs) { return bs.BoneId == lstBones->SelectedIndex; });
                if (bsId == -1)
                {
                    MorphState::BoneState newBs;
                    newBs.BoneId = lstBones->SelectedIndex;
                    newBs.Transform.Rotation = Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
                    newBs.Transform.Translation = sourceModel->GetSkeleton()->Bones[newBs.BoneId].BindPose.Translation;
                    retargetFile.MorphStates[editingMorphStateId].BoneStates.Add(newBs);
                    bsId = retargetFile.MorphStates[editingMorphStateId].BoneStates.Count() - 1;
                }
                retargetFile.MorphStates[editingMorphStateId].BoneStates[bsId].Transform.Translation = originalTranslation;
                Matrix4 originAnimTransform, originAnimParentTransform;
                GetAccumTransform(lstBones->SelectedIndex, originAnimTransform, originAnimParentTransform);
                auto targetAnimTransform = originAnimTransform;
                targetAnimTransform.SetTranslation(targetAnimTransform.GetTranslation() + e.TranslationOffset);
                Matrix4 invOriginalAnimParentTransform;
                originAnimParentTransform.Inverse(invOriginalAnimParentTransform);
                auto localTransform = invOriginalAnimParentTransform * targetAnimTransform;
                retargetFile.MorphStates[editingMorphStateId].BoneStates[bsId].Transform.Translation =
                    localTransform.GetTranslation();
            }
            else
            {
				targetSkeletonTransform.values[12] = oldTargetSkeletonTransform.values[12] + e.TranslationOffset.x;
				targetSkeletonTransform.values[13] = oldTargetSkeletonTransform.values[13] + e.TranslationOffset.y;
				targetSkeletonTransform.values[14] = oldTargetSkeletonTransform.values[14] + e.TranslationOffset.z;
            }
		}
		else if (IsScaleHandle(e.Handle))
		{
			targetSkeletonTransform.values[0] = oldTargetSkeletonTransform.values[0] * e.Scale.x;
			targetSkeletonTransform.values[5] = oldTargetSkeletonTransform.values[5] * e.Scale.y;
			targetSkeletonTransform.values[10] = oldTargetSkeletonTransform.values[10] * e.Scale.z;
		}
		else if (IsRotationHandle(e.Handle))
		{
			Vec3 axis;
			switch (e.Handle)
			{
			case ManipulationHandleType::RotationX: axis = Vec3::Create(1.0f, 0.0f, 0.0f); break;
			case ManipulationHandleType::RotationY: axis = Vec3::Create(0.0f, 1.0f, 0.0f); break;
			case ManipulationHandleType::RotationZ: axis = Vec3::Create(0.0f, 0.0f, 1.0f); break;
			}

            switch (editorMode)
            {
            case EditorMode::EditPreRotation: {
                retargetFile.PreRotations[lstBones->SelectedIndex] = originalTransform;
                UpdateCombinedRetargetTransform();
                auto rot = Quaternion::FromAxisAngle(axis, e.RotationAngle);
                Quaternion originAnimRot, originAnimParentRot, animRotLocal;
                GetAccumRotation(lstBones->SelectedIndex, animRotLocal, originAnimRot, originAnimParentRot);
                auto targetAnimRot = rot * originAnimRot;
                retargetFile.PreRotations[lstBones->SelectedIndex] = originAnimParentRot.Inverse() * targetAnimRot * animRotLocal.Inverse();
                UpdateCombinedRetargetTransform();
                break;
            }
            case EditorMode::EditMorphState: {
                int bsId = retargetFile.MorphStates[editingMorphStateId].BoneStates.FindFirst(
                    [&](auto &bs) { return bs.BoneId == lstBones->SelectedIndex; });
                if (bsId == -1)
                {
                    MorphState::BoneState newBs;
                    newBs.BoneId = lstBones->SelectedIndex;
                    newBs.Transform.Rotation = Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
                    newBs.Transform.Translation = sourceModel->GetSkeleton()->Bones[newBs.BoneId].BindPose.Translation;
                    retargetFile.MorphStates[editingMorphStateId].BoneStates.Add(newBs);
                    bsId = retargetFile.MorphStates[editingMorphStateId].BoneStates.Count() - 1;
                }
                retargetFile.MorphStates[editingMorphStateId].BoneStates[bsId].Transform.Rotation = originalTransform;
                auto rot = Quaternion::FromAxisAngle(axis, e.RotationAngle);
                Quaternion animRotLocal, originAnimRot, originAnimParentRot;
                GetAccumRotation(lstBones->SelectedIndex, animRotLocal, originAnimRot, originAnimParentRot);
                auto targetAnimRot = rot * originAnimRot;
                retargetFile.MorphStates[editingMorphStateId].BoneStates[bsId].Transform.Rotation =
                    originAnimParentRot.Inverse() * targetAnimRot;
                break;
            }
            }
            
		}
	}

	int undoPtr = -1;
    void RecoverFromStack()
    {
        switch (undoStack[undoPtr].editorMode)
        {
        case EditorMode::EditPreRotation:
            originalTransform = retargetFile.PreRotations[undoStack[undoPtr].boneId] =
                undoStack[undoPtr].originalTransform;
            UpdateCombinedRetargetTransform();
            break;
        case EditorMode::EditMorphState: {
            int msId = retargetFile.MorphStates.FindFirst(
                [&](auto &ms) { return ms.Name == undoStack[undoPtr].morphStateName; });
            if (msId != -1)
            {
                int bsId = retargetFile.MorphStates[msId].BoneStates.FindFirst(
                    [&](auto &bs) { return bs.BoneId == undoStack[undoPtr].boneId; });
                if (bsId != -1)
                {
                    retargetFile.MorphStates[msId].BoneStates[bsId].Transform.Rotation =
                        undoStack[undoPtr].originalTransform;
                    retargetFile.MorphStates[msId].BoneStates[bsId].Transform.Translation =
                        undoStack[undoPtr].originalTranslation;
                }
                originalTransform = undoStack[undoPtr].originalTransform;
                originalTranslation = undoStack[undoPtr].originalTranslation;
            }
            break;
        }
        }
    }
	void mnUndo_Clicked(UI_Base *)
	{
		if (sourceModel && undoPtr >= 0)
		{
            RecoverFromStack();
			undoPtr--;
		}
	}

	void mnRedo_Clicked(UI_Base *)
	{
		if (sourceModel && undoPtr < undoStack.Count() - 1)
		{
			undoPtr++;
            RecoverFromStack();
		}
	}

	void OnKeyDown(UI_Base *, GraphicsUI::UIKeyEventArgs & e)
	{
        if (e.Shift == SS_CONTROL && e.Key == '1')
            morphStateManipulationMode = ManipulationMode::Translation;
        if (e.Shift == SS_CONTROL && e.Key == '2')
            morphStateManipulationMode = ManipulationMode::Rotation;
		if (e.Shift == SS_CONTROL && (e.Key == 'Z' || e.Key == 'z'))
			mnUndo_Clicked(nullptr);
		if (e.Shift == SS_CONTROL && (e.Key == 'Y' || e.Key == 'y'))
			mnRedo_Clicked(nullptr);
		if (e.Shift == SS_CONTROL && (e.Key == 'T' || e.Key == 't'))
			mnToggleSkeletonModel_Clicked(nullptr);
		if (e.Shift == 0 && e.Key == ' ')
			mnPlayAnim_Clicked(nullptr);
		if (e.Shift == 0 && (e.Key == 'M' || e.Key == 'm'))
			mnMappingMode_Clicked(nullptr);
	}

	void CreateMorphState(GraphicsUI::UI_Base *sender)
    {
        if (lstMorphStates->SelectedIndex != -1)
        {
            auto nameLabel = reinterpret_cast<Label *>(lstMorphStates->GetSelectedItem())->GetText();
            if (retargetFile.MorphStates.FindFirst([&](auto &ms) { return ms.Name == nameLabel; }) == -1)
            {
                retargetFile.MorphStates.Add(MorphState());
                auto &newState = retargetFile.MorphStates.Last();
                newState.Name = nameLabel;
                UpdateMorphStateView();
                int index = lstMorphStates->Items.FindFirst(
                    [&](auto *item) { return static_cast<Label *>(item)->GetText() == nameLabel; });
                lstMorphStates->SelectedIndex = index;
                EditMorphState(sender);
            }
        }
    }

	void DeleteMorphState(GraphicsUI::UI_Base *sender)
    {
        if (lstMorphStates->SelectedIndex != -1)
        {
            int msId = retargetFile.MorphStates.FindFirst([&](auto &ms) {
                return ms.Name == lstMorphStates->GetTextItem(lstMorphStates->SelectedIndex)->GetText();
            });
            if (msId != -1)
            {
                editingMorphStateId = -1;
                editorMode = EditorMode::EditPreRotation;
                btnEditMorphState->Checked = false;
                retargetFile.MorphStates.RemoveAt(msId);
                UpdateMorphStateView();
            }
        }
    }

	void ResetMorphState(GraphicsUI::UI_Base *sender)
    {
        if (lstMorphStates->SelectedIndex != -1)
        {
            int msId = retargetFile.MorphStates.FindFirst([&](auto &ms) {
                return ms.Name == lstMorphStates->GetTextItem(lstMorphStates->SelectedIndex)->GetText();
            });
            if (msId != -1)
            {
                retargetFile.MorphStates[msId].BoneStates.Clear();
            }
        }
    }

    void EditMorphState(GraphicsUI::UI_Base *sender)
    {
        btnEditMorphState->Checked = !btnEditMorphState->Checked;
        if (btnEditMorphState->Checked)
        {
            editingMorphStateId = lstMorphStates->SelectedIndex;
            if (editingMorphStateId != -1 && retargetFile.MorphStates.Count() <= editingMorphStateId)
            {
                editingMorphStateId = -1;
                editorMode = EditorMode::EditPreRotation;
                btnEditMorphState->Checked = false;
                UpdateEditorMode();
                return;
            }
        }
        editorMode = btnEditMorphState->Checked ? EditorMode::EditMorphState : EditorMode::EditPreRotation;
        lblEditorMode->Visible = editorMode == EditorMode::EditMorphState;
        editingMorphStateId = lstMorphStates->SelectedIndex;
        UpdateEditorMode();
    }


	virtual void RegisterUI(GraphicsUI::UIEntry * pUiEntry) override
	{
		GraphicsUI::Global::Colors.EditableAreaBackColor = Color(40, 40, 40, 255);
		this->uiEntry = pUiEntry;
		manipulator = new GraphicsUI::TransformManipulator(uiEntry);
		manipulator->OnPreviewManipulation.Bind(this, &SkeletonRetargetVisualizerActor::PreviewManipulation);
		manipulator->OnApplyManipulation.Bind(this, &SkeletonRetargetVisualizerActor::ApplyManipulation);

		auto menu = new GraphicsUI::Menu(uiEntry, GraphicsUI::Menu::msMainMenu);
		menu->BackColor = GraphicsUI::Color(50, 50, 50, 255);
		uiEntry->MainMenu = menu;
		auto mnFile = new GraphicsUI::MenuItem(menu, "&File");
		auto mnEdit = new GraphicsUI::MenuItem(menu, "&Edit");
		auto mnView = new GraphicsUI::MenuItem(menu, "&View");
		auto mnUndo = new GraphicsUI::MenuItem(mnEdit, "Undo", "Ctrl+Z");
		mnUndo->OnClick.Bind(this, &SkeletonRetargetVisualizerActor::mnUndo_Clicked);
		auto mnRedo = new GraphicsUI::MenuItem(mnEdit, "Redo", "Ctrl+Y");
		mnRedo->OnClick.Bind(this, &SkeletonRetargetVisualizerActor::mnRedo_Clicked);
		uiEntry->OnKeyDown.Bind(this, &SkeletonRetargetVisualizerActor::OnKeyDown);
		
		auto mnViewBindPose = new GraphicsUI::MenuItem(mnView, "Bind Pose");
		mnViewBindPose->OnClick.Bind(this, &SkeletonRetargetVisualizerActor::ViewBindPose);

		auto mnToggleSkeletonModel = new GraphicsUI::MenuItem(mnView, "Toggle Skeleton/Model", "Ctrl+T");
		mnToggleSkeletonModel->OnClick.Bind(this, &SkeletonRetargetVisualizerActor::mnToggleSkeletonModel_Clicked);

		auto mnLoadSourceMesh = new GraphicsUI::MenuItem(mnFile, "Load Model...");
		mnLoadSourceMesh->OnClick.Bind(this, &SkeletonRetargetVisualizerActor::LoadSourceModel);


		auto mnLoadTargetSkeleton = new GraphicsUI::MenuItem(mnFile, "Load Animation Skeleton...");
		mnLoadTargetSkeleton->OnClick.Bind(this, &SkeletonRetargetVisualizerActor::LoadTargetSkeleton);

		auto mnLoadAnim = new GraphicsUI::MenuItem(mnFile, "Load Animation...");
		mnLoadAnim->OnClick.Bind(this, &SkeletonRetargetVisualizerActor::LoadAnimation);

		new GraphicsUI::MenuItem(mnFile);
		auto mnOpenSkeleton = new GraphicsUI::MenuItem(mnFile, "Open Retarget File...");
		mnOpenSkeleton->OnClick.Bind(this, &SkeletonRetargetVisualizerActor::Open);
		
		auto mnSaveSkeleton = new GraphicsUI::MenuItem(mnFile, "Save Retarget File...");
		mnSaveSkeleton->OnClick.Bind(this, &SkeletonRetargetVisualizerActor::Save);

		auto mnShowSourceSkeletonShape = new GraphicsUI::MenuItem(mnView, "Source Skeleton Shape");
		mnShowSourceSkeletonShape->OnClick.Bind(this, &SkeletonRetargetVisualizerActor::mnShowSourceSkeletonShape_Clicked);
		auto mnShowTargetSkeletonShape = new GraphicsUI::MenuItem(mnView, "Target Skeleton Shape");
		mnShowTargetSkeletonShape->OnClick.Bind(this, &SkeletonRetargetVisualizerActor::mnShowTargetSkeletonShape_Clicked);
		auto mnPlayAnimation = new GraphicsUI::MenuItem(mnView, "Play/Stop Animation", "Space");
		mnPlayAnimation->OnClick.Bind(this, &SkeletonRetargetVisualizerActor::mnPlayAnim_Clicked);
		auto mnMappingMode = new GraphicsUI::MenuItem(mnView, "Mapping Mode", "M");
		mnMappingMode->OnClick.Bind(this, &SkeletonRetargetVisualizerActor::mnMappingMode_Clicked);
		auto mnTargetSkeletonTransformMode = new GraphicsUI::MenuItem(mnView, "Target Skeleton Transform Mode");
		auto mnTranslationTransformMode = new GraphicsUI::MenuItem(mnTargetSkeletonTransformMode, "Translate");
		mnTranslationTransformMode->OnClick.Bind(this, &SkeletonRetargetVisualizerActor::mnTranslationTransformMode_Clicked);
		auto mnScaleTransformMode = new GraphicsUI::MenuItem(mnTargetSkeletonTransformMode, "Scale");
		mnScaleTransformMode->OnClick.Bind(this, &SkeletonRetargetVisualizerActor::mnScaleTransformMode_Clicked);

		auto pnl = new GraphicsUI::Container(uiEntry);
		pnl->BackColor = GraphicsUI::Color(50, 50, 50, 255);
		pnl->DockStyle = GraphicsUI::Control::dsLeft;
		pnl->SetWidth(EM(12.0f));
		pnl->Padding = EM(0.5f);

		auto pnl2 = new GraphicsUI::Container(pnl);
		pnl2->SetHeight(EM(19.0f));
		pnl2->DockStyle = GraphicsUI::Control::dsTop;
		
		auto addLabel = [&](Container* parent, String label, float left, float top, float width = 0.f, float height = 0.f)
		{
			auto lbl = new GraphicsUI::Label(parent);
			lbl->Posit(EM(left), EM(top), 0, 0);
			lbl->SetText(label);
            return lbl;
		};

		addLabel(pnl2, "Root Translation Scale", 0.0f, 0.0f, 4.0f, 1.0f);

		addLabel(pnl2, "X", 1.8f, 1.0f);
		txtScaleX = new GraphicsUI::TextBox(pnl2);
		txtScaleX->Posit(EM(0.5f), EM(2.0f), EM(2.8f), EM(1.0f));
		txtScaleX->SetText("1.0");
		txtScaleX->OnChanged.Bind(this, &SkeletonRetargetVisualizerActor::ChangeScaleX);

		addLabel(pnl2, "Y", 5.5f, 1.0f);
		txtScaleY = new GraphicsUI::TextBox(pnl2);
		txtScaleY->Posit(EM(4.2f), EM(2.0f), EM(2.8f), EM(1.0f));
		txtScaleY->SetText("1.0");
		txtScaleY->OnChanged.Bind(this, &SkeletonRetargetVisualizerActor::ChangeScaleY);

		addLabel(pnl2, "Z", 9.2f, 1.0f);
		txtScaleZ = new GraphicsUI::TextBox(pnl2);
		txtScaleZ->Posit(EM(7.9f), EM(2.0f), EM(2.8f), EM(1.0f));
		txtScaleZ->SetText("1.0");
		txtScaleZ->OnChanged.Bind(this, &SkeletonRetargetVisualizerActor::ChangeScaleZ);

		addLabel(pnl2, "Rotation", 0.0f, 4.0f, 4.0f, 1.0f);

		addLabel(pnl2, "X", 1.8f, 5.0f);
		txtX = new GraphicsUI::TextBox(pnl2);
		txtX->Posit(EM(0.5f), EM(6.0f), EM(2.8f), EM(1.0f));
		txtX->OnChanged.Bind(this, &SkeletonRetargetVisualizerActor::ChangeRotation);

		addLabel(pnl2, "Y", 5.5f, 5.0f);
		txtY = new GraphicsUI::TextBox(pnl2);
		txtY->Posit(EM(4.2f), EM(6.0f), EM(2.8f), EM(1.0f));
		txtY->OnChanged.Bind(this, &SkeletonRetargetVisualizerActor::ChangeRotation);

		addLabel(pnl2, "Z", 9.2f, 5.0f);
		txtZ = new GraphicsUI::TextBox(pnl2);
		txtZ->Posit(EM(7.9f), EM(6.0f), EM(2.8f), EM(1.0f));
		txtZ->OnChanged.Bind(this, &SkeletonRetargetVisualizerActor::ChangeRotation);
		
		auto lblBones = new GraphicsUI::Label(pnl2);
		lblBones->SetText("Bones");
		lblBones->DockStyle = GraphicsUI::Control::dsBottom;
		lstBones = new GraphicsUI::ListBox(pnl);
		lstBones->DockStyle = GraphicsUI::Control::dsFill;
		lstBones->OnClick.Bind(this, &SkeletonRetargetVisualizerActor::SelectedBoneChanged);
		txtInfo = GraphicsUI::CreateMultiLineTextBox(pnl2);
		txtInfo->Posit(EM(0.0f), EM(9.0f), EM(11.0f), EM(8.5f));

		auto rightPanel = new GraphicsUI::Container(uiEntry);
        rightPanel->DockStyle = GraphicsUI::Control::dsRight;
        rightPanel->BackColor = pnl->BackColor;
        rightPanel->SetWidth(EM(14.0f));
        
		pnlMorphState = new GraphicsUI::Container(rightPanel);
        pnlMorphState->Padding = EM(0.5f);
        pnlMorphState->DockStyle = GraphicsUI::Control::dsTop;
        pnlMorphState->SetHeight(EM(18.0f));
        auto lblMorphStates = addLabel(pnlMorphState, "Morph States", 0.0f, 0.0f, (float)EM(2.0f), (float)EM(1.2f));
        lblMorphStates->SetHeight(EM(1.5f));
        lblMorphStates->DockStyle = GraphicsUI::Control::dsTop;
        lstMorphStates = new GraphicsUI::ListBox(pnlMorphState);
        lstMorphStates->ManageItemFontColor = false;
        lstMorphStates->Posit(0, 0, rightPanel->GetWidth() - EM(1.0f), EM(12.0f));
        auto btnCreateMorphState = new GraphicsUI::Button(pnlMorphState);
        btnCreateMorphState->SetText("Create");
        btnCreateMorphState->Posit(
            lstMorphStates->Left, lstMorphStates->Top + lstMorphStates->GetHeight(), EM(4.0f), EM(1.5f));
        btnCreateMorphState->OnClick.Bind(this, &SkeletonRetargetVisualizerActor::CreateMorphState);
        auto btnDeleteMorphState = new GraphicsUI::Button(pnlMorphState);
        btnDeleteMorphState->SetText("Delete");
        btnDeleteMorphState->Posit(
            lstMorphStates->Left + EM(4.5f), lstMorphStates->Top + lstMorphStates->GetHeight(), EM(4.0f), EM(1.5f));
        btnDeleteMorphState->OnClick.Bind(this, &SkeletonRetargetVisualizerActor::DeleteMorphState);
        auto btnResetMorphState = new GraphicsUI::Button(pnlMorphState);
        btnResetMorphState->SetText("Reset");
        btnResetMorphState->Posit(
            lstMorphStates->Left + EM(9.0f), lstMorphStates->Top + lstMorphStates->GetHeight(), EM(4.0f), EM(1.5f));
        btnResetMorphState->OnClick.Bind(this, &SkeletonRetargetVisualizerActor::ResetMorphState);
        btnEditMorphState = new GraphicsUI::Button(pnlMorphState);
        btnEditMorphState->SetText("Edit");
        btnEditMorphState->Posit(
            lstMorphStates->Left, btnResetMorphState->Top + EM(2.0f), EM(13.0f), EM(1.5f));
        btnEditMorphState->OnClick.Bind(this, &SkeletonRetargetVisualizerActor::EditMorphState);
        auto pnlBoneMappingContainer = new GraphicsUI::Container(rightPanel);
        pnlBoneMappingContainer->Padding = EM(0.5f);
        pnlBoneMappingContainer->DockStyle = GraphicsUI::Control::dsFill;
        auto lblBoneMapping =
            addLabel(pnlBoneMappingContainer, "Bone Mapping", 0.0f, 0.0f, (float)EM(10.0f), (float)EM(1.2f));
        lblBoneMapping->Padding.Bottom = EM(0.2f);
        lblBoneMapping->DockStyle = GraphicsUI::Control::dsTop;
        pnlBoneMapping = new GraphicsUI::VScrollPanel(pnlBoneMappingContainer);
        pnlBoneMapping->DockStyle = GraphicsUI::Control::dsFill;

		auto pnlBottom = new GraphicsUI::Container(uiEntry);
		pnlBottom->BackColor = pnl->BackColor;
		pnlBottom->Padding = EM(0.5f);
		pnlBottom->Padding.Top = EM(0.3f);
		pnlBottom->SetHeight(EM(3.0f));
		pnlBottom->DockStyle = GraphicsUI::Control::dsBottom;
		lblFrame = new GraphicsUI::Label(pnlBottom);
		lblFrame->Posit(0, EM(0.0f), EM(10.0f), EM(1.0f));
		lblFrame->SetText("BindPose");
		scTimeline = new GraphicsUI::ScrollBar(pnlBottom);
		scTimeline->SetValue(0, 10, 0, 1);
		scTimeline->DockStyle = GraphicsUI::Control::dsBottom;
		scTimeline->OnChanged.Bind(this, &SkeletonRetargetVisualizerActor::scTimeline_Changed);

		infoForm = new GraphicsUI::Form(uiEntry);
		infoForm->Posit(EM(10.0f), EM(10.0f), EM(20.0f), EM(15.0f));
		infoForm->SetText("Info");
		infoFormTextBox = CreateMultiLineTextBox(infoForm);
		infoFormTextBox->DockStyle = GraphicsUI::Control::dsFill;
		infoForm->SizeChanged();
		uiEntry->CloseWindow(infoForm);

		lblEditorMode = new GraphicsUI::Label(uiEntry);
		lblEditorMode->Posit(EM(0.5f), EM(0.5f), 100, 30);
		lblEditorMode->SetText("Bone Mapping Mode On");
		lblEditorMode->FontColor = GraphicsUI::Color(0xD8, 0x43, 0x15, 255);
		lblEditorMode->Visible = false;

		uiEntry->OnMouseDown.Bind(this, &SkeletonRetargetVisualizerActor::WindowMouseDown);
		uiEntry->OnDblClick.Bind(this, &SkeletonRetargetVisualizerActor::WindowDblClick);
	}
	
	virtual void OnLoad() override
	{
		Actor::OnLoad();
		auto actualName = Engine::Instance()->FindFile("SkeletonVisualize.material", ResourceType::Material);

		sourceSkeletonMaterial.LoadFromFile(actualName);
		targetSkeletonMaterial.LoadFromFile(actualName);
		actualName = Engine::Instance()->FindFile("BoneHighlight.material", ResourceType::Material);
		sourceMeshMaterial.LoadFromFile(actualName);
		targetSkeletonMaterial.SetVariable("solidColor", Vec3::Create(0.2f, 0.3f, 0.9f));
		targetSkeletonMaterial.SetVariable("alpha", 0.8f);
		targetSkeletonMaterial.SetVariable("highlightColor", Vec3::Create(0.9f, 0.3f, 0.1f));
		
		auto solidMaterialFileName = Engine::Instance()->FindFile("SolidColor.material", ResourceType::Material);
		xAxisMaterial.LoadFromFile(solidMaterialFileName);
		xAxisMaterial.SetVariable("solidColor", Vec3::Create(0xF4 / 255.0f, 0x43 / 255.0f, 0x36 / 255.0f));
		yAxisMaterial.LoadFromFile(solidMaterialFileName);
		yAxisMaterial.SetVariable("solidColor", Vec3::Create(0x4C / 255.0f, 0xAF / 255.0f, 0x50 / 255.0f));
		zAxisMaterial.LoadFromFile(solidMaterialFileName);
		zAxisMaterial.SetVariable("solidColor", Vec3::Create(0x21 / 255.0f, 0x96 / 255.0f, 0xF3 / 255.0f));
		undoStack.Clear();
		undoPtr = -1;

		Matrix4::Translation(targetSkeletonTransform, 200.0f, 0.0f, 0.0f);
		oldTargetSkeletonTransform = targetSkeletonTransform;
	}
	
	virtual void OnUnload() override
	{
		Actor::OnUnload();
	}
	
	void WindowDblClick(UI_Base *)
	{
		if (lstBones->SelectedIndex != -1)
			((ArcBallCameraControllerActor*)(level->FindActor("CamControl")))->SetCenter(bonePositions[lstBones->SelectedIndex]);
	}


	void WindowMouseDown(UI_Base *, GraphicsUI::UIMouseEventArgs & e)
	{
		if (e.Shift & SS_ALT)
			return;
		Ray r = Engine::Instance()->GetRayFromMousePosition(e.X, e.Y);
		auto traceRs = level->GetPhysicsScene().RayTraceFirst(r, PhysicsChannels::Collision);
		targetSkeletonSelected = false;
		if (traceRs.Object)
		{
			if (traceRs.Object->Tag == (void*)2)
			{
				if (editorMode == EditorMode::EditMapping)
				{
					if (traceRs.Object->SkeletalBoneId != -1 && lstBones->SelectedIndex != -1)
					{
						targetSkeletonMaterial.SetVariable("highlightId", traceRs.Object->SkeletalBoneId);
						SetBoneMapping(lstBones->SelectedIndex, traceRs.Object->SkeletalBoneId);
						UpdateBoneMappingPanel();
					}
				}
				else
				{
					targetSkeletonSelected = true;
					lstBones->SetSelectedIndex(-1);
				}
			}
			else if (traceRs.Object->SkeletalBoneId != -1)
			{
				lstBones->SetSelectedIndex(traceRs.Object->SkeletalBoneId);
				SelectedBoneChanged(nullptr);
			}
		}
		else
		{
			lstBones->SetSelectedIndex(-1);
			SelectedBoneChanged(nullptr);
		}
	}

	Pose curPose;

	virtual void GetDrawables(const GetDrawablesParameter & param) override
	{
		Matrix4 identity;
		Matrix4::CreateIdentityMatrix(identity);
		if (!xAxisDrawable)
		{
			MeshBuilder mb;
			mb.AddBox(Vec3::Create(1.5f, -1.5f, -1.5f), Vec3::Create(100.0f, 1.5f, 1.5f));
			auto mesh = mb.ToMesh();
			xAxisDrawable = param.rendererService->CreateStaticDrawable(&mesh, 0, &xAxisMaterial, false);
			xAxisDrawable->UpdateTransformUniform(identity);
		}
		param.sink->AddDrawable(xAxisDrawable.Ptr());
		if (!yAxisDrawable)
		{
			MeshBuilder mb;
			mb.AddBox(Vec3::Create(-1.5f, 0.0f, -1.5f), Vec3::Create(1.5f, 100.0f, 1.5f));
			auto mesh = mb.ToMesh();
			yAxisDrawable = param.rendererService->CreateStaticDrawable(&mesh, 0, &yAxisMaterial, false);
			yAxisDrawable->UpdateTransformUniform(identity);
			
		}
		param.sink->AddDrawable(yAxisDrawable.Ptr());
		if (!zAxisDrawable)
		{
			MeshBuilder mb;
			mb.AddBox(Vec3::Create(-1.5f, -1.5f, 1.5f), Vec3::Create(1.5f, 1.5f, 100.0f));
			auto mesh = mb.ToMesh();
			zAxisDrawable = param.rendererService->CreateStaticDrawable(&mesh, 0, &zAxisMaterial, false);
			zAxisDrawable->UpdateTransformUniform(identity);
		}
		param.sink->AddDrawable(zAxisDrawable.Ptr());
		if (sourceModel)
		{
			auto sourceSkeleton = sourceModel->GetSkeleton();
			Model highlightModel(sourceModel->GetMesh(), sourceModel->GetSkeleton(), &sourceMeshMaterial);
			if (sourceModelInstance.IsEmpty())
			{
				sourceModelInstance = sourceModel->GetDrawableInstance(param);
				highlightModelInstance = highlightModel.GetDrawableInstance(param);
			}
			Pose p;
			p.Transforms.SetSize(sourceModel->GetSkeleton()->Bones.Count());
			for (int i = 0; i < p.Transforms.Count(); i++)
				p.Transforms[i] = sourceSkeleton->Bones[i].BindPose;
			if (showSourceModel)
			{
				for (auto & d : sourceModelInstance.Drawables)
				{
					d->Bounds.Min = Vec3::Create(-1e9f);
					d->Bounds.Max = Vec3::Create(1e9f);
					param.sink->AddDrawable(d.Ptr());
				}
				for (auto & d : highlightModelInstance.Drawables)
				{
					param.sink->AddDrawable(d.Ptr());
				}
			}
			if (!sourceSkeletonDrawable)
				sourceSkeletonDrawable = param.rendererService->CreateSkeletalDrawable(&sourceSkeletonMesh, 0, sourceModel->GetSkeleton(), &sourceSkeletonMaterial, false);
			
			p = GetCurrentPose();
            sourceSkeletonDrawable->UpdateTransformUniform(identity, p, &retargetFile, nullptr);
			sourceModelInstance.UpdateTransformUniform(identity, p, &retargetFile, nullptr);
            highlightModelInstance.UpdateTransformUniform(identity, p, &retargetFile, nullptr);
			skeletonPhysInstance->SetTransform(identity, p, &retargetFile);
			physInstance->SetTransform(identity, p, &retargetFile);

			curPose = p;
			if (!showSourceModel)
				param.sink->AddDrawable(sourceSkeletonDrawable.Ptr());
		}
		if (targetSkeleton)
		{
			if (!targetSkeletonDrawable)
			{
				targetSkeletonDrawable = param.rendererService->CreateSkeletalDrawable(&targetSkeletonMesh, 0, targetSkeleton.Ptr(), &targetSkeletonMaterial, false);
			}
			Pose p = GetCurrentPose();
			targetSkeletonDrawable->UpdateTransformUniform(targetSkeletonTransform, p, nullptr);
			targetSkeletonPhysInstance->SetTransform(targetSkeletonTransform, p, nullptr);
			param.sink->AddDrawable(targetSkeletonDrawable.Ptr());
		}
	}
};

void RegisterRetargetActor()
{
	Engine::Instance()->RegisterActorClass("SkeletonRetargetVisualizer", []() {return new SkeletonRetargetVisualizerActor(); });
}

#endif