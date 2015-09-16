var numeric = require('numeric');


// Convert a map of {anchor_id: range_in_meters} to a 3D location.
// Anchor locations is a map of {anchor_id: [X, Y, Z]}.
function calculate_location (anchor_ranges, anchor_locations) {

	// First need two arrays, one of the ranges to the anchors,
	// and one of the anchor locations.
	ranges = [];
	locations = [];
	for (var anchor_id in anchor_ranges) {
		if (anchor_ranges.hasOwnProperty(anchor_id)) {
			ranges.push(anchor_ranges[anchor_id]);
			locations.push(anchor_locations[anchor_id]);
		}
	}

	// Now that we have those data structures setup, we can run
	// the optimization function that calculates the most likely
	// position.

	// location_optimize([0,0,0], ranges, locations);
	// location_optimize(actual['test1'], ranges, locations);

	var location_optimize_with_args = function (start) {
		return location_optimize(start, ranges, locations);
	};

	var best = numeric.uncmin(location_optimize_with_args, [0,0,0]);
	console.log(best.solution)
	return best.solution;

}

// function location_optimize (args) {
// 	var tag_position = args[0];
// 	var anchor_ranges = args[1];
// 	var anchor_locations = args[2];
function location_optimize (tag_position, anchor_ranges, anchor_locations) {

	// console.log('starting pos: ')
	// console.log(tag_position);

	// First create an array that is the same length as the number of
	// ranges that contains the initial tag_position duplicated.
	var start = [];
	for (var i=0; i<anchor_ranges.length; i++) {
		start.push(tag_position);
	}

	// Do a distance calculate between tag and anchor known locations
	var next = [];
	var out = [];
	for (var i=0; i<anchor_ranges.length; i++) {
		next.push([]);
		// Subtract
		next[i][0] = start[i][0] - anchor_locations[i][0];
		next[i][1] = start[i][1] - anchor_locations[i][1];
		next[i][2] = start[i][2] - anchor_locations[i][2];

		// ^2
		next[i][0] = next[i][0] * next[i][0];
		next[i][1] = next[i][1] * next[i][1];
		next[i][2] = next[i][2] * next[i][2];

		// sum and sqrt
		var dist = Math.sqrt(next[i][0] + next[i][1] + next[i][2]);

		// Now subtract from that the range to that anchor, and square that
		var sub = dist - anchor_ranges[i];
		out.push(sub*sub);
	}
	// console.log(next);
	// console.log(out);

	// Sum the range comparisons and return that value
	var end = out.reduce(function(a, b){return a+b;});

	// console.log(end);
	return end;
}


module.exports = {
	calculate_location: calculate_location,
};
