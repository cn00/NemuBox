/* $Id: cconvAYUV.c $ */
#extension GL_ARB_texture_rectangle : enable
uniform sampler2DRect uSrcTex;
void nemuCConvApplyAYUV(vec4 color);
void nemuCConv()
{
    vec2 srcCoord = vec2(gl_TexCoord[0]);
    vec4 color = texture2DRect(uSrcTex, srcCoord);
    nemuCConvApplyAYUV(color);
}
