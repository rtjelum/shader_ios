// 3D gradient noise from iq's https://www.shadertoy.com/view/Xsl3Dl
vec3 hash( vec3 p ) // replace this by something better
{
	p = vec3( dot(p,vec3(127.1,311.7, 74.7)),
			  dot(p,vec3(269.5,183.3,246.1)),
			  dot(p,vec3(113.5,271.9,124.6)));

	return -1.0 + 2.0*fract(sin(p)*43758.5453123);
}
float noise( in vec3 p )
{
    vec3 i = floor( p );
    vec3 f = fract( p );
	
	vec3 u = f*f*(3.0-2.0*f);

    return mix( mix( mix( dot( hash( i + vec3(0.0,0.0,0.0) ), f - vec3(0.0,0.0,0.0) ), 
                          dot( hash( i + vec3(1.0,0.0,0.0) ), f - vec3(1.0,0.0,0.0) ), u.x),
                     mix( dot( hash( i + vec3(0.0,1.0,0.0) ), f - vec3(0.0,1.0,0.0) ), 
                          dot( hash( i + vec3(1.0,1.0,0.0) ), f - vec3(1.0,1.0,0.0) ), u.x), u.y),
                mix( mix( dot( hash( i + vec3(0.0,0.0,1.0) ), f - vec3(0.0,0.0,1.0) ), 
                          dot( hash( i + vec3(1.0,0.0,1.0) ), f - vec3(1.0,0.0,1.0) ), u.x),
                     mix( dot( hash( i + vec3(0.0,1.0,1.0) ), f - vec3(0.0,1.0,1.0) ), 
                          dot( hash( i + vec3(1.0,1.0,1.0) ), f - vec3(1.0,1.0,1.0) ), u.x), u.y), u.z );
}

// Metaballs and analytic normals from Klems' https://www.shadertoy.com/view/4dj3zV
void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    vec3 a, q, p, gradient, dir;
    float b, dist;
    dir = normalize(vec3((2.*fragCoord.xy-iResolution.xy)/min(iResolution.x,iResolution.y), 1.7));
    p = vec3(0, 0, -7);
    for(int i = 0; i < 100; i++) {
        q = p; // save current position
        p += dir * dist; // step
        gradient = vec3(0);
        dist = 0.;
        for(float j = 0.; j < 8.; j++) {
            vec3 ballp = sin(vec3(1,2,4) * j + iTime * .3) * 3.; // ball position
            b = dot(a = p - ballp, a);
            gradient += a / (b * b);
            dist += 1. / b;
        }
        dist = 1. - dist;
        if(dist < .001) { // if we've hit the metaballs
            dir = reflect(dir, normalize(gradient)); // set new reflected marching direction
            p = q; // restore previous position
            dist = 0.; // and don't step in this iteration
        }
     }
    vec3 col = dir + 1.;
    fragColor.rgb = noise(col * 2. + iTime * .3) * col * 2.;
}
