#ifndef ADDUCT_H
#define ADDUCT_H

#include "standardincludes.h"

using namespace std;

class Adduct {
    public:
        Adduct();
        Adduct(string name, int nmol, int charge, float mass);
        Adduct(const Adduct& a);

        bool operator==(const Adduct& other);
        bool operator<(const Adduct& other);
        string getName();
        int getCharge();
        int getNmol();
        float getMass();

        /**
         * @brief Predicate function which allows querying whether this adduct
         * represents a parent ion (i.e., a molecule associated to a singly-
         * charged hydrogen ion).
         * @return true if the molecule is a parent ion, false otherwise.
         */
        bool isParent() const;

        /**
         * @brief Given adduct's mass, compute parent ion's mass.
         * @param mz The m/z value of the adduct molecule.
         * @return The calculated neutral mass of parent ion.
         */
        float computeParentMass(float mz);

        /**
         * @brief Given parent ion's mass, compute adduct's m/z value.
         * @param parentMass The neutral mass of the parent ion.
         * @return The calculated m/z of adduct molecule.
         */
        float computeAdductMz(float parentMass);

    private:
        /**
         * @brief String representation of this adduct ion.
         */
        string _name;

        /**
         * @brief Number of parent molecules that have been fused through this
         * ion.
         */
        int _nmol;

        /**
         * @brief The amount of charge possessed by the adduct ion.
         */
        int _charge;

        /**
         * @brief Mass of the independent charged ion, when it is not
         * associated to a parent ion.
         */
        float _mass;
};

#endif
