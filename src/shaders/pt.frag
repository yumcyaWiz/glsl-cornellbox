#version 330 core

#include common/global.frag
#include common/uniform.frag
#include common/rng.frag
#include common/raygen.frag
#include common/util.frag
#include common/intersect.frag
#include common/closest_hit.frag
#include common/sampling.frag
#include common/brdf.frag

in vec2 texCoord;

layout (location = 0) out vec3 color;
layout (location = 1) out uint state;

vec3 computeRadiance(in Ray ray_in) {
    Ray ray = ray_in;

    float russian_roulette_prob = 1;
    vec3 color = vec3(0);
    vec3 throughput = vec3(1);

    for(int i = 0; i < MAX_DEPTH; ++i) {
        // russian roulette
        if(random() >= russian_roulette_prob) {
            break;
        }
        throughput /= russian_roulette_prob;

        IntersectInfo info;
        if(intersect(ray, info)) {
            Primitive hitPrimitive = primitives[info.primID];
            Material hitMaterial = materials[hitPrimitive.material_id];
            vec3 wo = -ray.direction;
            vec3 wo_local = worldToLocal(wo, info.dpdu, info.hitNormal, info.dpdv);

            // Le 
            if(any(greaterThan(hitMaterial.le, vec3(0)))) {
                color += throughput * hitMaterial.le;
                break;
            }

            // BRDF Sampling
            float pdf;
            vec3 wi_local;
            vec3 brdf = sampleBRDF(wo_local, wi_local, hitMaterial, pdf);
            // prevent NaN
            if(pdf == 0.0) {
                break;
            }
            vec3 wi = localToWorld(wi_local, info.dpdu, info.hitNormal, info.dpdv);

            // update throughput
            float cos_term = abs(wi_local.y);
            throughput *= brdf * cos_term / pdf;

            // update russian roulette probability
            russian_roulette_prob = min(max(max(throughput.x, throughput.y), throughput.z), 1.0);

            // set next ray
            ray = Ray(info.hitPos, wi);
        }
        else {
            color += throughput * vec3(0);
            break;
        }
    }

    return color;
}

void main() {
    // set RNG seed
    setSeed(texCoord);

    // generate initial ray
    vec2 uv = (2.0*(gl_FragCoord.xy + vec2(random(), random())) - resolution) * resolutionYInv;
    uv.y = -uv.y;
    float pdf;
    Ray ray = rayGen(uv, pdf);
    float cos_term = dot(camera.camForward, ray.direction);

    // accumulate sampled color on accumTexture
    vec3 radiance = computeRadiance(ray) / pdf;
    color = texture(accumTexture, texCoord).xyz + radiance * cos_term;

    // save RNG state on stateTexture
    state = RNG_STATE.a;
}