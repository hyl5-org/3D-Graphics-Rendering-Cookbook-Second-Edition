const float M_PI = 3.141592653589793;

float pow2(float x)
{
	return x * x;
}

mat3 getTangentBasis(vec3 tangentZ)
{
	const float sign_ = tangentZ.z >= 0 ? 1 : -1;
	const float a = -1.0 / sqrt(sign_ + tangentZ.z);
	const float b = tangentZ.x * tangentZ.y * a;

	vec3 tangentX = vec3(1 + sign_ * a * pow2(tangentZ.x), sign_ * b, -sign_ * tangentZ.x);
	vec3 tangentY = vec3(b, sign_ + a * pow2(tangentZ.y), -tangentZ.y);

	return mat3(tangentX, tangentY, tangentZ);
}

vec4 cosineSampleHemisphere(vec2 e)
{
	float phi = 2.0 * M_PI * e.x;
	float cosTheta = sqrt(e.y);
	float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

	vec3 h;
	h.x = sinTheta * cos(phi);
	h.y = sinTheta * sin(phi);
	h.z = cosTheta;

	float pdf = cosTheta * (1.0 / M_PI);

	return vec4(h, pdf);
}

// 128x128x64 blue noise stored as 128x8192 tiled texture (64 slices in Y)
vec2 blueNoiseVec2(uint texBlueNoise, uint smpl, vec2 screenCoord, uint frameIndex)
{
	uvec2 wrappedXY = uvec2(screenCoord) & uvec2(127);
	uint wrappedZ = frameIndex & 63u;
	vec2 uv = (vec2(float(wrappedXY.x), float(wrappedZ * 128u + wrappedXY.y)) + vec2(0.5)) / vec2(128.0, 8192.0);
	return textureBindless2D(texBlueNoise, smpl, uv).rg;
}

vec4 uniformSampleConeRobust(vec2 e, float sinThetaMax2)
{
	float phi = 2.0 * M_PI * e.x;
	float oneMinusCosThetaMax = sinThetaMax2 < 0.01 ? sinThetaMax2 * (0.5 + 0.125 * sinThetaMax2) : 1.0 - sqrt(1.0 - sinThetaMax2);

	float cosTheta = 1.0 - oneMinusCosThetaMax * e.y;
	float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

	vec3 l;
	l.x = sinTheta * cos(phi);
	l.y = sinTheta * sin(phi);
	l.z = cosTheta;
	float pdf = 1.0 / (2.0 * M_PI * oneMinusCosThetaMax);

	return vec4(l, pdf);
}

vec3 generateSoftShadowRayDir(vec3 lightDir, float lightRadius, vec2 randSample)
{
	float sinThetaMax2 = lightRadius * lightRadius;
	vec4 coneSample = uniformSampleConeRobust(randSample, sinThetaMax2);
	mat3 basis = getTangentBasis(lightDir);
	return normalize(basis * coneSample.xyz);
}
