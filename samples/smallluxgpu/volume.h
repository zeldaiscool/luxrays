/***************************************************************************
 *   Copyright (C) 1998-2010 by authors (see AUTHORS.txt )                 *
 *                                                                         *
 *   This file is part of LuxRays.                                         *
 *                                                                         *
 *   LuxRays is free software; you can redistribute it and/or modify       *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   LuxRays is distributed in the hope that it will be useful,            *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 *                                                                         *
 *   LuxRays website: http://www.luxrender.net                             *
 ***************************************************************************/

#ifndef _VOLUME_H
#define	_VOLUME_H

#include <vector>

#include "smalllux.h"
#include "sampler.h"

class Scene;

class VolumeIntegrator {
public:
	virtual ~VolumeIntegrator() { }

	virtual Spectrum Transmittance(const Ray &ray) const = 0;
	virtual bool GenerateLiRays(const Scene *scene, Sample *sample, const unsigned int maxVectorSize,
		const Ray &ray, vector<Ray> &volumeRays, vector<unsigned int> currentVolueRayIndex,
		Spectrum *Lvolume) const = 0;
};

class SingleScatteringIntegrator : public VolumeIntegrator {
public:
	SingleScatteringIntegrator(const BBox &bbox, const float step,
			Spectrum absorption, Spectrum scattering, Spectrum emission) {
		region = bbox;
		stepSize = step;
		sig_a = absorption;
		sig_s = scattering;
		lightEmission = emission;
	}

	Spectrum Transmittance(const Ray &ray) const {
		return Exp(-HomogenousTau(ray));
	}

	bool GenerateLiRays(const Scene *scene, Sample *sample, const unsigned int maxVectorSize,
		const Ray &ray, vector<Ray> &volumeRays, vector<unsigned int> currentVolueRayIndex,
		Spectrum *Lvolume) const;

private:
	Spectrum HomogenousTau(const Ray &ray) const {
		float t0, t1;
        if (!region.IntersectP(ray, &t0, &t1))
			return 0.f;

        return Distance(ray(t0), ray(t1)) * (sig_a + sig_s);
	}

	BBox region;
	float stepSize;
	Spectrum sig_a, sig_s, lightEmission;
};

#endif	/* _VOLUME_H */