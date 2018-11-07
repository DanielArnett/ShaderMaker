#include "FisheyeRotation.h"
#include <ffgl/FFGLLib.h>
#include <ffglex/FFGLScopedShaderBinding.h>
#include <ffglex/FFGLScopedSamplerActivation.h>
#include <ffglex/FFGLScopedTextureBinding.h>
using namespace ffglex;

#define FFPARAM_Roll ( 0 )
#define FFPARAM_Pitch ( 1 )
#define FFPARAM_Yaw ( 2 )

static CFFGLPluginInfo PluginInfo(
	PluginFactory< FisheyeRotation >,// Create method
	"FROT",                       // Plugin unique ID of maximum length 4.
	"Fisheye Rotation",		 	  // Plugin name
	2,                            // API major version number
	1,                            // API minor version number
	1,                            // Plugin major version number
	0,                            // Plugin minor version number
	FF_EFFECT,                    // Plugin type
	"Rotate Fisheye videos",	  // Plugin description
	"Written by Daniel Arnett, go to https://github.com/DanielArnett/360-VJ/releases for more detail."      // About
);

static const char vertexShaderCode[] = R"(#version 410 core
uniform vec2 MaxUV;

layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;

out vec2 uv;

void main()
{
	gl_Position = vPosition;
	uv = vUV * MaxUV;
}
)";

static const char fragmentShaderCode[] = R"(#version 410 core
uniform sampler2D InputTexture;
uniform vec3 Brightness;

in vec2 uv;
/*
 * This is used to manipulate 360 VR videos, also known as equirectangular videos.
 * This shader can pitch, roll, and yaw the camera within a 360 image.
*/
vec3 PRotateX(vec3 p, float theta)
{
   vec3 q;
   q.x = p.x;
   q.y = p.y * cos(theta) + p.z * sin(theta);
   q.z = -p.y * sin(theta) + p.z * cos(theta);
   return(q);
}

vec3 PRotateY(vec3 p, float theta)
{
   vec3 q;
   q.x = p.x * cos(theta) - p.z * sin(theta);
   q.y = p.y;
   q.z = p.x * sin(theta) + p.z * cos(theta);
   return(q);
}

vec3 PRotateZ(vec3 p, float theta)
{
   vec3 q;
   q.x = p.x * cos(theta) + p.y * sin(theta);
   q.y = -p.x * sin(theta) + p.y * cos(theta);
   q.z = p.z;
   return(q);
}
out vec4 fragColor;
float PI = 3.14159265359;
void main()
{
	//vec2 shiftXYInput = (vec2(2.0,2.0) * vec2(inputColour.r, inputColour.g) - vec2(1.0,1.0)) * iResolution.xy;
	vec2 shiftXYInput = vec2(0.0,0.0);
	// Get inputs from Resolume
	float rotateXInput = Brightness.r * 2.0 - 1.0;
	float rotateZInput = Brightness.g * 2.0 - 1.0; // -0.5 to 0.5
	float rotateYInput = Brightness.b * 2.0 - 1.0; // -0.5 to 0.5
	// Position of the destination pixel in xy coordinates in the range [-1,1]
	vec2 pos = 2.0 * uv - 1.0;

	// Radius of the pixel from the center
	float r = sqrt(pos.x*pos.x + pos.y*pos.y);
	// Don't bother with pixels outside of the fisheye circle
	if (1.0 < r) {
		// Return a transparent pixel
		fragColor = vec4(0.0,0.0,0.0,0.0);
		return;
	}
	float phi;
	float latitude = (1.0 - r)*(PI / 2.0);
	float longitude;
	float u;
	float v;
	// The ray into the scene
	vec3 p;
	// Output color. In our case the color of the source pixel
	vec3 col;
	// Set the source pixel's coordinates
	vec2 outCoord;
	// Calculate longitude
	if (r == 0.0) {
		longitude = 0.0;
	}
	else if (pos.x < 0.0) {
		longitude = PI - asin(pos.y / r);
	}
	else if (pos.x >= 0.0) {
		longitude = asin(pos.y / r);
	}
	// Perform z rotation.
	longitude += rotateZInput * 2.0 * PI;
	if (longitude < 0.0) {
		longitude += 2.0*PI;
	}
	// Turn the latitude and longitude into a 3D ray
	p.x = cos(latitude) * cos(longitude);
	p.y = cos(latitude) * sin(longitude);
	p.z = sin(latitude);
	// Rotate the value based on the user input
	p = PRotateX(p, 2.0 * PI * rotateXInput);
	p = PRotateY(p, 2.0 * PI * rotateYInput);
	// Get the source pixel latitude and longitude
	latitude = asin(p.z);
	longitude = -acos(p.x / cos(latitude));
	// Get the source pixel radius from center
	r = 1.0 - latitude/(PI / 2.0);
	// Disregard all images outside of the fisheye circle
	if (r > 1.0) {
		return;
	}

	// Manually implement `phi = atan2(p.y, p.x);`
	if (p.x > 0.0) {
		phi = atan(p.y / p.x);
	}
	else if (p.x < 0.0 && p.y >= 0.0) {
		phi = atan(p.y / p.x) + PI;
	}
	else if (p.x < 0.0 && p.y < 0.0) {
		phi = atan(p.y / p.x) - PI;
	}
	else if (p.x == 0.0 && p.y > 0.0) {
		phi = PI / 2.0;
	}
	else if (p.x == 0.0 && p.y < 0.0) {
		phi = -PI / 2.0;
	}
	if (phi < 0.0) {
		phi += 2.0*PI;
	}

	// Get the position of the output pixel
	u = r * cos(phi);
	v = r * sin(phi);
	// Normalize the output pixel to be in the range [0,1]
	outCoord.x = (float(u) + 1.0) / 2.0;
	outCoord.y = (float(v) + 1.0) / 2.0;
	outCoord += shiftXYInput;
	// Set the color of the destination pixel to the color of the source pixel.
	fragColor = texture(InputTexture, outCoord);
}
)";

FisheyeRotation::FisheyeRotation() :
	maxUVLocation( -1 ),
	fieldOfViewLocation( -1 ),
	aspectRatio( 0.5f ),
	yaw( 0.5f ),
	fieldOfView( 0.5f )
{
	// Input properties
	SetMinInputs( 1 );
	SetMaxInputs( 1 );

	// Parameters
	SetParamInfof( FFPARAM_Roll, "Roll", FF_TYPE_STANDARD );
	SetParamInfof( FFPARAM_Pitch, "Pitch", FF_TYPE_STANDARD );
	SetParamInfof( FFPARAM_Yaw, "Yaw", FF_TYPE_STANDARD );
}
FisheyeRotation::~FisheyeRotation()
{
}

FFResult FisheyeRotation::InitGL( const FFGLViewportStruct* vp )
{
	if( !shader.Compile( vertexShaderCode, fragmentShaderCode ) )
	{
		DeInitGL();
		return FF_FAIL;
	}
	if( !quad.Initialise() )
	{
		DeInitGL();
		return FF_FAIL;
	}

	//FFGL requires us to leave the context in a default state on return, so use this scoped binding to help us do that.
	ScopedShaderBinding shaderBinding( shader.GetGLID() );

	//We're never changing the sampler to use, instead during rendering we'll make sure that we're always
	//binding the texture to sampler 0.
	glUniform1i( shader.FindUniform( "inputTexture" ), 0 );

	//We need to know these uniform locations because we need to set their value each frame.
	maxUVLocation = shader.FindUniform( "MaxUV" );
	fieldOfViewLocation = shader.FindUniform( "Brightness" );

	//Use base-class init as success result so that it retains the viewport.
	return CFreeFrameGLPlugin::InitGL( vp );
}
FFResult FisheyeRotation::ProcessOpenGL( ProcessOpenGLStruct* pGL )
{
	if( pGL->numInputTextures < 1 )
		return FF_FAIL;

	if( pGL->inputTextures[ 0 ] == NULL )
		return FF_FAIL;

	//FFGL requires us to leave the context in a default state on return, so use this scoped binding to help us do that.
	ScopedShaderBinding shaderBinding( shader.GetGLID() );

	FFGLTextureStruct& Texture = *( pGL->inputTextures[ 0 ] );

	//The input texture's dimension might change each frame and so might the content area.
	//We're adopting the texture's maxUV using a uniform because that way we dont have to update our vertex buffer each frame.
	FFGLTexCoords maxCoords = GetMaxGLTexCoords( Texture );
	glUniform2f( maxUVLocation, maxCoords.s, maxCoords.t );

	glUniform3f( fieldOfViewLocation,
				 -1.0f + ( aspectRatio * 2.0f ),
				 -1.0f + ( yaw * 2.0f ),
				 -1.0f + ( fieldOfView * 2.0f ) );

	//The shader's sampler is always bound to sampler index 0 so that's where we need to bind the texture.
	//Again, we're using the scoped bindings to help us keep the context in a default state.
	ScopedSamplerActivation activateSampler( 0 );
	Scoped2DTextureBinding textureBinding( Texture.Handle );

	quad.Draw();

	return FF_SUCCESS;
}
FFResult FisheyeRotation::DeInitGL()
{
	shader.FreeGLResources();
	quad.Release();
	maxUVLocation = -1;
	fieldOfViewLocation = -1;

	return FF_SUCCESS;
}

FFResult FisheyeRotation::SetFloatParameter( unsigned int dwIndex, float value )
{
	switch( dwIndex )
	{
	case FFPARAM_Pitch:
		aspectRatio = value;
		break;
	case FFPARAM_Yaw:
		yaw = value;
		break;
	case FFPARAM_Roll:
		fieldOfView = value;
		break;
	default:
		return FF_FAIL;
	}

	return FF_SUCCESS;
}

float FisheyeRotation::GetFloatParameter( unsigned int dwIndex )
{
	switch( dwIndex )
	{
	case FFPARAM_Pitch:
		return aspectRatio;
	case FFPARAM_Yaw:
		return yaw;
	case FFPARAM_Roll:
		return fieldOfView;

	default:
		return 0.0f;
	}
}
