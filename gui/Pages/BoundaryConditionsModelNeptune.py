# -*- coding: utf-8 -*-

#-------------------------------------------------------------------------------

# This file is part of Code_Saturne, a general-purpose CFD tool.
#
# Copyright (C) 1998-2018 EDF S.A.
#
# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; either version 2 of the License, or (at your option) any later
# version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
# details.
#
# You should have received a copy of the GNU General Public License along with
# this program; if not, write to the Free Software Foundation, Inc., 51 Franklin
# Street, Fifth Floor, Boston, MA 02110-1301, USA.

#-------------------------------------------------------------------------------

import sys, unittest
from code_saturne.Base.XMLvariables import Model
from code_saturne.Base.XMLengine import *
from code_saturne.Base.XMLmodelNeptune import *
from code_saturne.Pages.MainFieldsModel import *


class BoundaryConditionsModel(MainFieldsModel, Variables, Model):

    """
    This class manages the boundary conditions objects in the XML file
    """

    def __init__(self, case):
        """
        Constuctor.
        """
        #
        # XML file parameters
        self.case = case
        MainFieldsModel.__init__(self, case)

#-------------------------------------------------------------------------------
# DefineUsersScalars test case
#-------------------------------------------------------------------------------
class BoundaryConditionsTestCase(ModelTest):
    """
    """
    def checkBoundaryConditionsInstantiation(self):
        """Check whether the BoundaryConditions class could be instantiated"""
        model = None
        model = BoundaryConditionsModel(self.case)
        assert model != None, 'Could not instantiate BoundaryConditionsModel'


def suite():
    testSuite = unittest.makeSuite(BoundaryConditionsTestCase, "check")
    return testSuite


def runTest():
    print("BoundaryConditionsCase")
    runner = unittest.TextTestRunner()
    runner.run(suite())
