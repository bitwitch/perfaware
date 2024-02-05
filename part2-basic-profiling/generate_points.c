#define _CRT_SECURE_NO_WARNINGS 
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define EARTH_RADIUS_KM 6372.8

typedef double F64;

void *xrealloc(void *ptr, size_t new_size) {
	void *result = realloc(ptr, new_size);
	if (!result) {
		perror("realloc");
		exit(1);
	}
	return result;
}

F64 lerp_f64(F64 min, F64 max, F64 t) {
	return min + t * (max-min);
}

// returns a random float from min to max, both inclusive
F64 random_f64(F64 min, F64 max) {
	assert(max - min <= RAND_MAX);
	F64 normalized = rand() / (F64)RAND_MAX;
	return lerp_f64(min, max, normalized);
}


/* ========================================================================
   from LISTING 65 - Reference Haversine Distance Formula
   ======================================================================== */


static F64 square(F64 a) {
    return a*a;
}

static F64 radians_from_degrees(F64 degrees) {
    return 0.01745329251994329577f * degrees;
}

// NOTE(casey): earth_radius is generally expected to be 6372.8
static F64 reference_haversine(F64 x0, F64 y0, F64 x1, F64 y1, F64 earth_radius)
{
    /* NOTE(casey): This is not meant to be a "good" way to calculate the Haversine distance.
       Instead, it attempts to follow, as closely as possible, the formula used in the real-world
       question on which these homework exercises are loosely based.
    */
    
    F64 lat1 = y0;
    F64 lat2 = y1;
    F64 lon1 = x0;
    F64 lon2 = x1;
    
    F64 dLat = radians_from_degrees(lat2 - lat1);
    F64 dLon = radians_from_degrees(lon2 - lon1);
    lat1 = radians_from_degrees(lat1);
    lat2 = radians_from_degrees(lat2);
    
    F64 a = square(sin(dLat/2.0)) + cos(lat1)*cos(lat2)*square(sin(dLon/2));
    F64 c = 2.0*asin(sqrt(a));
    
    F64 result = earth_radius * c;
    
    return result;
}
/* ======================================================================== */

// min is inclusive, max is exclusive
typedef struct {
	F64 x_min, x_max, y_min, y_max;
} Cluster;

typedef struct {
	Cluster *clusters;
	int count;
	int cap;
} ClusterArray;

Cluster cluster_create(F64 x_min, F64 x_max, F64 y_min, F64 y_max) {
	return (Cluster){ 
		.x_min = x_min, .x_max = x_max, 
		.y_min = y_min, .y_max = y_max
	};
}

void cluster_append(ClusterArray *clusters, Cluster cluster) {
	if (clusters->count + 1 >= clusters->cap) {
		// resize
		clusters->cap = clusters->cap == 0 ? 2 : 2 * clusters->cap;
		size_t new_size = clusters->cap * sizeof(Cluster);
		clusters->clusters = xrealloc(clusters->clusters, new_size);
	}
	clusters->clusters[clusters->count++] = cluster;
}

Cluster choose_cluster(ClusterArray clusters) {
	int i = rand() % clusters.count;
	return clusters.clusters[i];
}

ClusterArray generate_clusters(void) {
	ClusterArray result = {0};
	F64 x = -180, y = -90;
	enum {
		max_x_divisions = 12,
		max_y_divisions = 7,
	};

	// maximum step in x and y
	F64 x_step = 60; 
	F64 y_step = 60;

	// get x divisions
	F64 x_divisions[max_x_divisions] = {-180};
	int x_division_count = 1;
	for (; x_division_count < max_x_divisions-1; ++x_division_count) {
		x += random_f64(1, x_step);
		if (x > 180) x = 180;
		x_divisions[x_division_count] = x;
		if (x == 180) break;
	}
	if (x < 180) {
		assert(x_division_count < max_x_divisions);
		x_divisions[x_division_count++] = 180;
	}

	// get y divisions
	F64 y_divisions[max_y_divisions] = {-90};
	int y_division_count = 1;
	for (; y_division_count < max_y_divisions-1; ++y_division_count) {
		y += random_f64(1, y_step);
		if (y > 90) y = 90;
		y_divisions[y_division_count] = y;
		if (y == 90) break;
	}
	if (y < 90) {
		assert(y_division_count < max_y_divisions);
		y_divisions[y_division_count++] = 90;
	}

	for (int j = 0; j < y_division_count - 1; ++j) {
		F64 y_min = y_divisions[j];
		F64 y_max = y_divisions[j+1];
		for (int i = 0; i < x_division_count - 1; ++i) {
			F64 x_min = x_divisions[i];
			F64 x_max = x_divisions[i+1];
			cluster_append(&result, cluster_create(x_min, x_max, y_min, y_max));
		}
	}

	return result;
}

bool cluster_flag_from_mode_string(char *mode) {
	bool cluster;
	enum { max_mode_chars = 16 };
	if (0 == strncmp(mode, "cluster", max_mode_chars)) {
		cluster = true;
	} else if (0 == strncmp(mode, "uniform", max_mode_chars)) {
		cluster = false;
	} else {
		printf("Mode argument must be uniform or cluster, got '%s'\n", mode);
		exit(1);
	}
	return cluster;
}

int main(int argc, char **argv) {
	if (argc < 4) {
		printf("Usage: %s [uniform/cluster] [random seed] [number of coordinate pairs to generate]\n", argv[0]);
		exit(1);
	}

	char *mode        = argv[1];
	bool cluster_mode = cluster_flag_from_mode_string(mode);
	int seed          = atoi(argv[2]);
	int num_pairs     = atoi(argv[3]);

	srand(seed);

	ClusterArray clusters = generate_clusters();

	char json_filepath[256];
	char computations_filepath[256];
	snprintf(json_filepath, 256, "data_%d_pairs.json", num_pairs);
	snprintf(computations_filepath, 256, "data_%d_computations.f64", num_pairs);

	FILE *json_file = fopen(json_filepath, "w");
	if (!json_file) { perror("fopen"); exit(1); }
	FILE *comp_file = fopen(computations_filepath, "wb");
	if (!comp_file) { perror("fopen"); exit(1); }

	fprintf(json_file, "{\"pairs\":[\n");

	F64 sum = 0;

	// generate pairs
	for (int i=0; i<num_pairs; ++i) {
		F64 x0, y0, x1, y1;
		if (cluster_mode) {
			Cluster cluster = choose_cluster(clusters);
			x0 = random_f64(cluster.x_min, cluster.x_max);
			y0 = random_f64(cluster.y_min, cluster.y_max);
			x1 = random_f64(cluster.x_min, cluster.x_max);
			y1 = random_f64(cluster.y_min, cluster.y_max);
		} else {
			x0 = random_f64(-180, 180);
			y0 = random_f64(-90, 90);
			x1 = random_f64(-180, 180);
			y1 = random_f64(-90, 90);
		}

		F64 reference_result = reference_haversine(x0, y0, x1, y1, EARTH_RADIUS_KM);
		sum += reference_result;

		// write computed distance to comp_file
		fwrite(&reference_result, sizeof(reference_result), 1, comp_file);

		// write pair to json_file
		if (i > 0) fprintf(json_file, ",\n");
		fprintf(json_file, "\t{\"x0\":%.16f, \"y0\":%.16f, \"x1\":%.16f, \"y1\":%.16f}", x0, y0, x1, y1);
	}

	assert(num_pairs > 0);
	F64 average = sum / num_pairs;

	// write average to comp_file
	fwrite(&average, sizeof(average), 1, comp_file);

	fprintf(json_file, "\n]}");

	fclose(comp_file);
	fclose(json_file);

	printf("Mode: %s\nRandom seed: %d\nPair count: %d\nAverage distance: %f\n", mode, seed, num_pairs, average);

	return 0;
}
