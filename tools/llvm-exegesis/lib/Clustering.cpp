//===-- Clustering.cpp ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Clustering.h"
#include "llvm/ADT/SmallVector.h"
#include <string>

namespace llvm {
namespace exegesis {

// The clustering problem has the following characteristics:
//  (A) - Low dimension (dimensions are typically proc resource units,
//    typically < 10).
//  (B) - Number of points : ~thousands (points are measurements of an MCInst)
//  (C) - Number of clusters: ~tens.
//  (D) - The number of clusters is not known /a priory/.
//  (E) - The amount of noise is relatively small.
// The problem is rather small. In terms of algorithms, (D) disqualifies
// k-means and makes algorithms such as DBSCAN[1] or OPTICS[2] more applicable.
//
// We've used DBSCAN here because it's simple to implement. This is a pretty
// straightforward and inefficient implementation of the pseudocode in [2].
//
// [1] https://en.wikipedia.org/wiki/DBSCAN
// [2] https://en.wikipedia.org/wiki/OPTICS_algorithm

// Finds the points at distance less than sqrt(EpsilonSquared) of Q (not
// including Q).
void InstructionBenchmarkClustering::rangeQuery(
    const size_t Q, std::vector<size_t> &Neighbors) const {
  Neighbors.clear();
  Neighbors.reserve(Points_.size() - 1); // The Q itself isn't a neighbor.
  const auto &QMeasurements = Points_[Q].Measurements;
  for (size_t P = 0, NumPoints = Points_.size(); P < NumPoints; ++P) {
    if (P == Q)
      continue;
    const auto &PMeasurements = Points_[P].Measurements;
    if (PMeasurements.empty()) // Error point.
      continue;
    if (isNeighbour(PMeasurements, QMeasurements)) {
      Neighbors.push_back(P);
    }
  }
}

InstructionBenchmarkClustering::InstructionBenchmarkClustering(
    const std::vector<InstructionBenchmark> &Points,
    const double EpsilonSquared)
    : Points_(Points), EpsilonSquared_(EpsilonSquared),
      NoiseCluster_(ClusterId::noise()), ErrorCluster_(ClusterId::error()) {}

llvm::Error InstructionBenchmarkClustering::validateAndSetup() {
  ClusterIdForPoint_.resize(Points_.size());
  // Mark erroneous measurements out.
  // All points must have the same number of dimensions, in the same order.
  const std::vector<BenchmarkMeasure> *LastMeasurement = nullptr;
  for (size_t P = 0, NumPoints = Points_.size(); P < NumPoints; ++P) {
    const auto &Point = Points_[P];
    if (!Point.Error.empty()) {
      ClusterIdForPoint_[P] = ClusterId::error();
      ErrorCluster_.PointIndices.push_back(P);
      continue;
    }
    const auto *CurMeasurement = &Point.Measurements;
    if (LastMeasurement) {
      if (LastMeasurement->size() != CurMeasurement->size()) {
        return llvm::make_error<llvm::StringError>(
            "inconsistent measurement dimensions",
            llvm::inconvertibleErrorCode());
      }
      for (size_t I = 0, E = LastMeasurement->size(); I < E; ++I) {
        if (LastMeasurement->at(I).Key != CurMeasurement->at(I).Key) {
          return llvm::make_error<llvm::StringError>(
              "inconsistent measurement dimensions keys",
              llvm::inconvertibleErrorCode());
        }
      }
    }
    LastMeasurement = CurMeasurement;
  }
  if (LastMeasurement) {
    NumDimensions_ = LastMeasurement->size();
  }
  return llvm::Error::success();
}

void InstructionBenchmarkClustering::dbScan(const size_t MinPts) {
  const size_t NumPoints = Points_.size();

  // Persistent buffers to avoid allocs.
  std::vector<size_t> Neighbors;
  std::vector<size_t> ToProcess(NumPoints);
  std::vector<char> Processed(NumPoints);

  for (size_t P = 0; P < NumPoints; ++P) {
    if (!ClusterIdForPoint_[P].isUndef())
      continue; // Previously processed in inner loop.
    rangeQuery(P, Neighbors);
    if (Neighbors.size() + 1 < MinPts) { // Density check.
      // The region around P is not dense enough to create a new cluster, mark
      // as noise for now.
      ClusterIdForPoint_[P] = ClusterId::noise();
      continue;
    }

    // Create a new cluster, add P.
    Clusters_.emplace_back(ClusterId::makeValid(Clusters_.size()));
    Cluster &CurrentCluster = Clusters_.back();
    ClusterIdForPoint_[P] = CurrentCluster.Id; /* Label initial point */
    CurrentCluster.PointIndices.push_back(P);
    Processed[P] = 1;

    // Enqueue P's neighbors.
    size_t Tail = 0;
    auto EnqueueUnprocessed = [&](const std::vector<size_t> &Neighbors) {
      for (size_t Q : Neighbors)
        if (!Processed[Q]) {
          ToProcess[Tail++] = Q;
          Processed[Q] = 1;
        }
    };
    EnqueueUnprocessed(Neighbors);

    for (size_t Head = 0; Head < Tail; ++Head) {
      // Retrieve a point from the queue and add it to the current cluster.
      P = ToProcess[Head];
      ClusterId OldCID = ClusterIdForPoint_[P];
      ClusterIdForPoint_[P] = CurrentCluster.Id;
      CurrentCluster.PointIndices.push_back(P);
      if (OldCID.isNoise())
        continue;
      assert(OldCID.isUndef());

      // And extend to the neighbors of P if the region is dense enough.
      rangeQuery(P, Neighbors);
      if (Neighbors.size() + 1 >= MinPts)
        EnqueueUnprocessed(Neighbors);
    }
  }

  // Add noisy points to noise cluster.
  for (size_t P = 0; P < NumPoints; ++P)
    if (ClusterIdForPoint_[P].isNoise())
      NoiseCluster_.PointIndices.push_back(P);
}

llvm::Expected<InstructionBenchmarkClustering>
InstructionBenchmarkClustering::create(
    const std::vector<InstructionBenchmark> &Points, const size_t MinPts,
    const double Epsilon) {
  InstructionBenchmarkClustering Clustering(Points, Epsilon * Epsilon);
  if (auto Error = Clustering.validateAndSetup()) {
    return std::move(Error);
  }
  if (Clustering.ErrorCluster_.PointIndices.size() == Points.size()) {
    return Clustering; // Nothing to cluster.
  }

  Clustering.dbScan(MinPts);
  return Clustering;
}

} // namespace exegesis
} // namespace llvm
