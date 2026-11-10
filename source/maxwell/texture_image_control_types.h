#pragma once

namespace maxwell
{

enum TicHdrVersion
{
	TicHdrVersion_1DBuffer            = 0,
	TicHdrVersion_PitchColorKey       = 1,
	TicHdrVersion_Pitch               = 2,
	TicHdrVersion_BlockLinear         = 3,
	TicHdrVersion_BlockLinearColorKey = 4,
};

enum TextureType
{
	TextureType_1D         = 0,
	TextureType_2D         = 1,
	TextureType_3D         = 2,
	TextureType_Cubemap    = 3,
	TextureType_1DArray    = 4,
	TextureType_2DArray    = 5,
	TextureType_1DBuffer   = 6,
	TextureType_2DNoMipmap = 7,
	TextureType_CubeArray  = 8,
};

enum MsaaMode
{
	MsaaMode_1x1      = 0,
	MsaaMode_2x1      = 1,
	MsaaMode_2x2      = 2,
	MsaaMode_4x2      = 3,
	MsaaMode_4x2_D3D  = 4,
	MsaaMode_2x1_D3D  = 5,
	MsaaMode_4x4      = 6,
	MsaaMode_2x2_VC4  = 8,
	MsaaMode_2x2_VC12 = 9,
	MsaaMode_4x2_VC8  = 10,
	MsaaMode_4x2_VC24 = 11,
};

enum LodQuality
{
	LodQuality_Low  = 0,
	LodQuality_High = 1,
};

enum SectorPromotion
{
	SectorPromotion_None = 0,
	SectorPromotion_To2V = 1,
	SectorPromotion_To2H = 2,
	SectorPromotion_To4  = 3,
};

enum BorderSize
{
	BorderSize_One          = 0,
	BorderSize_Two          = 1,
	BorderSize_Four         = 2,
	BorderSize_Eight        = 3,
	BorderSize_SamplerColor = 7,
};

enum AnisoSpreadModifier
{
	AnisoSpreadModifier_None = 0,
	AnisoSpreadModifier_One  = 1,
	AnisoSpreadModifier_Two  = 2,
	AnisoSpreadModifier_Sqrt = 3,
};

enum AnisoSpreadFunc
{
	AnisoSpreadFunc_Half = 0,
	AnisoSpreadFunc_One  = 1,
	AnisoSpreadFunc_Two  = 2,
	AnisoSpreadFunc_Max  = 3,
};

enum MaxAnisotropy
{
	MaxAnisotropy_1to1  = 0,
	MaxAnisotropy_2to1  = 1,
	MaxAnisotropy_4to1  = 2,
	MaxAnisotropy_6to1  = 3,
	MaxAnisotropy_8to1  = 4,
	MaxAnisotropy_10to1 = 5,
	MaxAnisotropy_12to1 = 6,
	MaxAnisotropy_16to1 = 7,
};

constexpr MsaaMode getMsaaMode(unsigned numSamplesLog2)
{
	switch (numSamplesLog2)
	{
		default:
		case 0: // DkMsMode_1x
			return MsaaMode_1x1;
		case 1: // DkMsMode_2x
			return MsaaMode_2x1_D3D;
		case 2: // DkMsMode_4x
			return MsaaMode_2x2;
		case 3: // DkMsMode_8x
			return MsaaMode_4x2_D3D;
		/* Currently not supported, maybe in the future
		case 4: // DkMsMode_16x
			return MsaaMode_4x4;
		*/
	}
}

}
