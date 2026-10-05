#include "engine/motion/Motion.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace Runtime {
namespace {

float Clamp01(float value) {
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

float Ease(const std::string& name, float t) {
    t = Clamp01(t);
    if (name == "easeIn") {
        return t * t;
    }
    if (name == "easeOut") {
        const float inverse = 1.0f - t;
        return 1.0f - inverse * inverse;
    }
    if (name == "easeInOut") {
        return t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) * 0.5f;
    }
    if (name == "cubicBezier") {
        return t * t * (3.0f - 2.0f * t);
    }
    return t;
}

float SpringValue(const MotionDecl& motion, float time) {
    const float mass = motion.mass <= 0.0f ? 1.0f : motion.mass;
    const float stiffness = motion.stiffness <= 0.0f ? 1.0f : motion.stiffness;
    const float omega = std::sqrt(stiffness / mass);
    const float zeta = motion.damping / (2.0f * std::sqrt(stiffness * mass));
    const float distance = motion.from - motion.to;
    float offset = distance;
    if (zeta < 1.0f) {
        const float damped = omega * std::sqrt(std::max(0.0f, 1.0f - zeta * zeta));
        const float decay = std::exp(-zeta * omega * time);
        const float wave = distance * std::cos(damped * time) +
                           ((motion.velocity + zeta * omega * distance) / std::max(0.001f, damped)) * std::sin(damped * time);
        offset = decay * wave;
    } else {
        offset = distance * std::exp(-omega * time);
    }
    return motion.to + offset;
}

bool TriggerAllows(const MotionDecl& motion, bool hover) {
    if (motion.trigger.empty()) {
        return true;
    }
    if (motion.trigger == "hover" || motion.trigger == "onHover") {
        return hover;
    }
    return true;
}

const MotionDecl* FindMotion(const std::vector<MotionDecl>& motions, const std::string& name) {
    for (const MotionDecl& motion : motions) {
        if (motion.name == name) {
            return &motion;
        }
    }
    return nullptr;
}

bool IsReferenced(const std::vector<MotionDecl>& motions, const std::string& name) {
    if (name.empty()) {
        return false;
    }
    for (const MotionDecl& motion : motions) {
        if (motion.sequence.find(name) != std::string::npos || motion.parallel.find(name) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> SplitList(const std::string& text) {
    std::vector<std::string> parts;
    std::string current;
    for (char character : text) {
        if (character == ',' || character == ' ') {
            if (!current.empty()) {
                parts.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(character);
    }
    if (!current.empty()) {
        parts.push_back(current);
    }
    return parts;
}

void SampleOne(const MotionDecl& motion, float localTime, std::vector<MotionSample>& samples) {
    if (localTime < 0.0f) {
        localTime = 0.0f;
    }
    if (!motion.keys.empty()) {
        const MotionKey* previous = &motion.keys.front();
        const MotionKey* next = &motion.keys.back();
        for (const MotionKey& key : motion.keys) {
            if (key.time <= localTime) {
                previous = &key;
            }
            if (key.time >= localTime) {
                next = &key;
                break;
            }
        }
        MotionSample sample;
        sample.target = motion.target;
        sample.property = previous->property.empty() ? motion.property : previous->property;
        if (previous->hasNumber && next->hasNumber && next->time > previous->time) {
            const float span = (localTime - previous->time) / (next->time - previous->time);
            const float eased = Ease(motion.easing == "spring" ? "linear" : motion.easing, span);
            sample.number = previous->number + (next->number - previous->number) * eased;
            sample.hasNumber = true;
        } else if (previous->hasNumber) {
            sample.number = previous->number;
            sample.hasNumber = true;
        }
        sample.text = localTime < (previous->time + next->time) * 0.5f ? previous->text : next->text;
        if (sample.text.empty()) {
            sample.text = previous->text;
        }
        samples.push_back(std::move(sample));
        return;
    }
    MotionSample sample;
    sample.target = motion.target;
    sample.property = motion.property;
    if (motion.easing == "spring") {
        sample.number = SpringValue(motion, localTime);
        sample.hasNumber = true;
    } else if (motion.hasFrom && motion.hasTo) {
        const float duration = motion.duration <= 0.0f ? 0.001f : motion.duration;
        const float eased = Ease(motion.easing, localTime / duration);
        sample.number = motion.from + (motion.to - motion.from) * eased;
        sample.hasNumber = true;
    }
    samples.push_back(std::move(sample));
}

void SampleNamed(const std::vector<MotionDecl>& motions, const std::string& name, float localTime, std::vector<MotionSample>& samples) {
    if (const MotionDecl* motion = FindMotion(motions, name)) {
        SampleOne(*motion, localTime, samples);
    }
}

}  // namespace

std::vector<MotionSample> SampleMotions(const std::vector<MotionDecl>& motions, float time, int staggerIndex, bool hover) {
    std::vector<MotionSample> samples;
    for (const MotionDecl& motion : motions) {
        if (!motion.name.empty() && IsReferenced(motions, motion.name)) {
            continue;
        }
        if (!TriggerAllows(motion, hover)) {
            continue;
        }
        const float staggered = time - static_cast<float>(std::max(0, staggerIndex)) * motion.stagger;
        if (!motion.sequence.empty()) {
            float cursor = 0.0f;
            for (const std::string& name : SplitList(motion.sequence)) {
                const MotionDecl* step = FindMotion(motions, name);
                if (step == nullptr) {
                    continue;
                }
                const float start = cursor;
                cursor += step->duration <= 0.0f ? 0.001f : step->duration;
                if (staggered >= start && staggered <= cursor) {
                    SampleOne(*step, staggered - start, samples);
                }
            }
            continue;
        }
        if (!motion.parallel.empty()) {
            for (const std::string& name : SplitList(motion.parallel)) {
                SampleNamed(motions, name, std::max(0.0f, staggered), samples);
            }
            continue;
        }
        SampleOne(motion, staggered, samples);
    }
    return samples;
}

}  // namespace Runtime
