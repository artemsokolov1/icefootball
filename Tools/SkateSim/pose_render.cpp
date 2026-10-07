// Renders the procedural skater pose (Core/SkatePose) to an SVG contact sheet:
// one row per scenario, columns = side view (X-Z), front view (Y-Z), top view (X-Y).
// Usage: ./pose_render > poses.svg   (see render_poses.sh)
#include "SkatePose.h"
#include "SkateTuningPresets.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
	struct FScenario
	{
		const char* Name;
		FSkatePoseInput Input;
		float WarmUp;      // seconds of steady input before the snapshot
	};

	enum class EView { Side, Front, Top };

	const float Scale = 1.6f;   // px per cm
	const float CellW = 330.f;
	const float CellH = 360.f;

	// Projects a skater-local point into a cell. Side: +X right, Z up. Front: viewer in front, +Y to the viewer's left.
	void Project(const FSkateVec3& P, EView View, float Cx, float Cy, float& Ox, float& Oy)
	{
		switch (View)
		{
		case EView::Side: Ox = Cx + P.X * Scale; Oy = Cy - P.Z * Scale; break;
		case EView::Front: Ox = Cx - P.Y * Scale; Oy = Cy - P.Z * Scale; break;
		case EView::Top: Ox = Cx + P.Y * Scale; Oy = Cy - P.X * Scale; break;
		}
	}

	struct FSvg
	{
		std::string Body;
		void Line(const FSkateVec3& A, const FSkateVec3& B, EView V, float Cx, float Cy, float Width, const char* Color)
		{
			float X0, Y0, X1, Y1;
			Project(A, V, Cx, Cy, X0, Y0);
			Project(B, V, Cx, Cy, X1, Y1);
			char Buf[256];
			std::snprintf(Buf, sizeof(Buf), "<line x1='%.1f' y1='%.1f' x2='%.1f' y2='%.1f' stroke='%s' stroke-width='%.1f' stroke-linecap='round' opacity='0.92'/>\n",
				X0, Y0, X1, Y1, Color, Width * Scale);
			Body += Buf;
		}
		void Circle(const FSkateVec3& C, EView V, float Cx, float Cy, float R, const char* Color)
		{
			float X, Y;
			Project(C, V, Cx, Cy, X, Y);
			char Buf[160];
			std::snprintf(Buf, sizeof(Buf), "<circle cx='%.1f' cy='%.1f' r='%.1f' fill='%s'/>\n", X, Y, R * Scale, Color);
			Body += Buf;
		}
		void Text(float X, float Y, const std::string& T, int Size = 14)
		{
			char Buf[512];
			std::snprintf(Buf, sizeof(Buf), "<text x='%.1f' y='%.1f' font-family='sans-serif' font-size='%d' fill='#222'>%s</text>\n", X, Y, Size, T.c_str());
			Body += Buf;
		}
	};

	void DrawPose(FSvg& Svg, const FSkatePose& P, EView V, float Cx, float Cy)
	{
		const char* Jersey = "#d63a1a";
		const char* Pants = "#1b2550";
		const char* Skin = "#e9b896";
		// Ice line / origin
		if (V != EView::Top)
		{
			Svg.Line(FSkateVec3(-60.f, -60.f, 0.f), FSkateVec3(60.f, 60.f, 0.f), V, Cx, Cy, 0.6f, "#7fa7c9");
		}
		// Back limbs first (left side), then torso, then right side.
		for (int Side = 0; Side < 2; ++Side)
		{
			Svg.Line(P.Hip[Side], P.Knee[Side], V, Cx, Cy, SkateBody::ThighThickness, Pants);
			Svg.Line(P.Knee[Side], P.Ankle[Side], V, Cx, Cy, SkateBody::ShinThickness, Pants);
			const FSkateVec3 Toe = P.Ankle[Side] + FSkateVec3(14.f, 0.f, -6.f).RotatedZ(P.FootYawDeg[Side]);
			const FSkateVec3 Heel = P.Ankle[Side] + FSkateVec3(-12.f, 0.f, -6.f).RotatedZ(P.FootYawDeg[Side]);
			Svg.Line(Heel, Toe, V, Cx, Cy, 9.f, "#111");
			const FSkateVec3 BladeA = FSkateVec3(Heel.X, Heel.Y, P.Ankle[Side].Z - 10.f);
			const FSkateVec3 BladeB = FSkateVec3(Toe.X, Toe.Y, P.Ankle[Side].Z - 10.f);
			Svg.Line(BladeA, BladeB, V, Cx, Cy, 1.5f, "#9aa");
		}
		// Torso as a thick segment (chest box width) + pelvis
		Svg.Line(P.Pelvis, P.Chest, V, Cx, Cy, V == EView::Side ? SkateBody::ChestDepth : SkateBody::ChestWidth, Jersey);
		Svg.Line(P.Hip[0], P.Hip[1], V, Cx, Cy, 16.f, Pants);
		Svg.Line(P.Shoulder[0], P.Shoulder[1], V, Cx, Cy, 12.f, Jersey);
		Svg.Circle(P.Head, V, Cx, Cy, SkateBody::HeadRadius, Skin);
		Svg.Circle(P.Head + P.TorsoForward * 9.f, V, Cx, Cy, 3.f, "#222"); // visor = facing
		for (int Side = 0; Side < 2; ++Side)
		{
			Svg.Line(P.Shoulder[Side], P.Elbow[Side], V, Cx, Cy, SkateBody::UpperArmThickness, Side == 0 ? "#b02a10" : Jersey);
			Svg.Line(P.Elbow[Side], P.Hand[Side], V, Cx, Cy, SkateBody::ForearmThickness, Side == 0 ? "#b02a10" : Jersey);
			Svg.Circle(P.Hand[Side], V, Cx, Cy, SkateBody::HandSize * 0.5f, "#222");
		}
	}
}

int main()
{
	const FSkateTuning Tuning = SkateTuningPresets::Make(ESkatePreset::Balanced);

	std::vector<FScenario> Scenarios;
	{
		FScenario S{ "Stance (standing)", FSkatePoseInput(), 1.f };
		Scenarios.push_back(S);
	}
	{
		FSkatePoseInput I; I.SpeedRatio = 0.6f; I.PushAmount = 1.f; I.ThrustAccel = 900.f;
		Scenarios.push_back({ "Push stroke (accelerating)", I, 1.13f });
		Scenarios.push_back({ "Push stroke, other leg", I, 1.45f });
	}
	{
		FSkatePoseInput I; I.SpeedRatio = 0.9f;
		Scenarios.push_back({ "Glide", I, 1.f });
	}
	{
		FSkatePoseInput I; I.SpeedRatio = 1.f; I.LateralAccel = -1300.f; I.PushAmount = 1.f; I.ThrustAccel = 200.f;
		Scenarios.push_back({ "Carve left (lean)", I, 1.f });
	}
	{
		FSkatePoseInput I; I.SpeedRatio = 0.7f; I.BrakeAmount = 1.f; I.SlideSide = 1.f;
		Scenarios.push_back({ "Hockey stop (LT)", I, 1.f });
	}
	{
		FSkatePoseInput I; I.SpeedRatio = 0.3f; I.KickCharge = 1.f;
		Scenarios.push_back({ "Kick wind-up (X held)", I, 1.f });
	}
	{
		FSkatePoseInput I; I.SpeedRatio = 0.3f; I.SwingKind = ESkateImpulseKind::Kick; I.SwingTime = 0.07f; I.SwingPower = 1.f;
		Scenarios.push_back({ "Kick swing", I, 0.f });
	}

	FSvg Svg;
	const float TopPad = 40.f;
	Svg.Text(10.f, 24.f, "Procedural skater pose (Core/SkatePose): side view (facing right) | front view | top view (forward = up)", 16);
	for (size_t Row = 0; Row < Scenarios.size(); ++Row)
	{
		const FScenario& Sc = Scenarios[Row];
		FSkatePoseState State;
		FSkatePose Pose;
		const float Dt = 1.f / 60.f;
		int Steps = static_cast<int>(Sc.WarmUp / Dt);
		if (Steps < 1) { Steps = 1; }
		for (int Index = 0; Index < Steps; ++Index)
		{
			FSkatePoseInput In = Sc.Input;
			if (Sc.Input.SwingKind == ESkateImpulseKind::Kick && Sc.WarmUp == 0.f)
			{
				// Snapshot during the swing (charge already released).
				In.KickCharge = 0.f;
			}
			Pose = FSkatePoseSolver::Update(Tuning.Anim, In, Dt, State);
		}
		const float Y0 = TopPad + Row * CellH;
		Svg.Text(10.f, Y0 + 18.f, std::string(Sc.Name) + "  [" + Pose.Label + "]");
		const float BaseY = Y0 + CellH - 30.f;
		DrawPose(Svg, Pose, EView::Side, 0.5f * CellW, BaseY);
		DrawPose(Svg, Pose, EView::Front, 1.5f * CellW, BaseY);
		DrawPose(Svg, Pose, EView::Top, 2.5f * CellW, Y0 + CellH * 0.55f);
	}
	const float W = 3.f * CellW;
	const float H = TopPad + Scenarios.size() * CellH;
	std::printf("<svg xmlns='http://www.w3.org/2000/svg' width='%.0f' height='%.0f'><rect width='100%%' height='100%%' fill='#f4f7fa'/>\n%s</svg>\n", W, H, Svg.Body.c_str());
	return 0;
}
