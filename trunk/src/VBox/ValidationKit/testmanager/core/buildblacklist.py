# -*- coding: utf-8 -*-
# $Id$

"""
Test Manager - Builds Blacklist.
"""

__copyright__ = \
"""
Copyright (C) 2012-2014 Oracle Corporation

This file is part of VirtualBox Open Source Edition (OSE), as
available from http://www.virtualbox.org. This file is free software;
you can redistribute it and/or modify it under the terms of the GNU
General Public License (GPL) as published by the Free Software
Foundation, in version 2 as it comes in the "COPYING" file of the
VirtualBox OSE distribution. VirtualBox OSE is distributed in the
hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.

The contents of this file may alternatively be used under the terms
of the Common Development and Distribution License Version 1.0
(CDDL) only, as it comes in the "COPYING.CDDL" file of the
VirtualBox OSE distribution, in which case the provisions of the
CDDL are applicable instead of those of the GPL.

You may elect to license modified versions of this file under the
terms and conditions of either the GPL or the CDDL or both.
"""
__version__ = "$Revision$"


# Validation Kit imports.
from testmanager.core.base          import ModelDataBase, ModelLogicBase, TMExceptionBase


class BuildBlacklistData(ModelDataBase):
    """
    Build Blacklist Data.
    """

    ksIdAttr = 'idBlacklisting';

    ksParam_idBlacklisting  = 'BuildBlacklist_idBlacklisting'
    ksParam_tsEffective     = 'BuildBlacklist_tsEffective'
    ksParam_tsExpire        = 'BuildBlacklist_tsExpire'
    ksParam_uidAuthor       = 'BuildBlacklist_uidAuthor'
    ksParam_idFailureReason = 'BuildBlacklist_idFailureReason'
    ksParam_sProduct        = 'BuildBlacklist_sProduct'
    ksParam_sBranch         = 'BuildBlacklist_sBranch'
    ksParam_asTypes         = 'BuildBlacklist_asTypes'
    ksParam_asOsArches      = 'BuildBlacklist_asOsArches'
    ksParam_iFirstRevision  = 'BuildBlacklist_iFirstRevision'
    ksParam_iLastRevision   = 'BuildBlacklist_iLastRevision'

    kasAllowNullAttributes    = [ 'idBlacklisting',
                                  'tsEffective',
                                  'tsExpire',
                                  'uidAuthor',
                                  'asTypes',
                                  'asOsArches' ];

    def __init__(self):
        ModelDataBase.__init__(self);

        #
        # Initialize with defaults.
        # See the database for explanations of each of these fields.
        #
        self.idBlacklisting  = None
        self.tsEffective     = None
        self.tsExpire        = None
        self.uidAuthor       = None
        self.idFailureReason = None
        self.sProduct        = None
        self.sBranch         = None
        self.asTypes         = None
        self.asOsArches      = None
        self.iFirstRevision  = None
        self.iLastRevision   = None

    def initFromDbRow(self, aoRow):
        """
        Re-initializes the data with a row from a SELECT * FROM BuildBlacklist.

        Returns self. Raises exception if the row is None or otherwise invalid.
        """

        if aoRow is None:
            raise TMExceptionBase('Build Blacklist item not found.')

        self.idBlacklisting  = aoRow[0]
        self.tsEffective     = aoRow[1]
        self.tsExpire        = aoRow[2]
        self.uidAuthor       = aoRow[3]
        self.idFailureReason = aoRow[4]
        self.sProduct        = aoRow[5]
        self.sBranch         = aoRow[6]
        self.asTypes         = aoRow[7]
        self.asOsArches      = aoRow[8]
        self.iFirstRevision  = aoRow[9]
        self.iLastRevision   = aoRow[10]

        return self;

    def initFromDbWithId(self, oDb, idBlacklisting, tsNow = None, sPeriodBack = None):
        """
        Initialize the object from the database.
        """
        oDb.execute(self.formatSimpleNowAndPeriodQuery(oDb,
                                                       'SELECT *\n'
                                                       'FROM   BuildBlacklist\n'
                                                       'WHERE  idBlacklisting   = %s\n'
                                                       , ( idBlacklisting,), tsNow, sPeriodBack));
        aoRow = oDb.fetchOne()
        if aoRow is None:
            raise TMExceptionBase('idBlacklisting=%s not found (tsNow=%s sPeriodBack=%s)'
                                  % (idBlacklisting, tsNow, sPeriodBack,));
        return self.initFromDbRow(aoRow);


class BuildBlacklistLogic(ModelLogicBase): # pylint: disable=R0903
    """
    Build Back List logic.
    """

    def fetchForListing(self, iStart, cMaxRows, tsNow):
        """
        Fetches Build Blacklist records.

        Returns an array (list) of BuildBlacklistData items, empty list if none.
        Raises exception on error.
        """

        if tsNow is None:
            self._oDb.execute('SELECT   *\n'
                              'FROM     BuildBlacklist\n'
                              'WHERE    tsExpire = \'infinity\'::TIMESTAMP\n'
                              'ORDER BY idBlacklisting DESC\n'
                              'LIMIT %s OFFSET %s\n'
                              , (cMaxRows, iStart,));
        else:
            self._oDb.execute('SELECT   *\n'
                              'FROM     BuildBlacklist\n'
                              'WHERE    tsExpire     > %s\n'
                              '     AND tsEffective <= %s\n'
                              'ORDER BY idBlacklisting DESC\n'
                              'LIMIT %s OFFSET %s\n'
                              , (tsNow, tsNow, cMaxRows, iStart,));

        aoRows = []
        for aoRow in self._oDb.fetchAll():
            aoRows.append(BuildBlacklistData().initFromDbRow(aoRow))
        return aoRows

    def addEntry(self, oData, uidAuthor, fCommit = False):
        """
        Adds a blacklisting to the database.
        """
        self._oDb.execute('INSERT INTO BuildBlacklist (\n'
                          '     uidAuthor,\n'
                          '     idFailureReason,\n'
                          '     sProduct,\n'
                          '     sBranch,\n'
                          '     asTypes,\n'
                          '     asOsArches,\n'
                          '     iFirstRevision,\n'
                          '     iLastRevision)\n'
                          'VALUES (%s, %s, %s, %s, %s, %s, %s, %s)'
                          , ( uidAuthor,
                              oData.idFailureReason,
                              oData.sProduct,
                              oData.sBranch,
                              oData.asTypes,
                              oData.asOsArches,
                              oData.iFirstRevision,
                              oData.iLastRevision,) );
        self._oDb.maybeCommit(fCommit);
        return True

    def editEntry(self, oData, uidAuthor, fCommit = False):
        """
        Modifies a blacklisting.
        """

        #
        # Validate inputs and read in the old(/current) data.
        #
        assert isinstance(oData, BuildBlacklistData);
        dErrors = oData.validateAndConvert(self._oDb);
        if len(dErrors) > 0:
            raise TMExceptionBase('editEntry invalid input: %s' % (dErrors,));

        oOldData = BuildBlacklistData().initFromDbWithId(self._oDb, oData.idBlacklisting);

        #
        # Update the data that needs updating.
        #
        if not oData.isEqualEx(oOldData, [ 'tsEffective', 'tsExpire', 'uidAuthor', ]):
            self._historizeEntry(oData.idBlacklisting, None);
            self._readdEntry(uidAuthor, oData, None);
        self._oDb.maybeCommit(fCommit);
        return True;


    def removeEntry(self, uidAuthor, idBlacklisting, fCascade = False, fCommit = False):
        """
        Deletes a test group.
        """
        _ = fCascade; # Not applicable.

        oData = BuildBlacklistData().initFromDbWithId(self._oDb, idBlacklisting);

        (tsCur, tsCurMinusOne) = self._oDb.getCurrentTimestamps();
        if oData.tsEffective != tsCur and oData.tsEffective != tsCurMinusOne:
            self._historizeEntry(idBlacklisting, tsCurMinusOne);
            self._readdEntry(uidAuthor, oData, tsCurMinusOne);
            self._historizeEntry(idBlacklisting);
        self._oDb.execute('UPDATE   BuildBlacklist\n'
                          'SET      tsExpire       = CURRENT_TIMESTAMP\n'
                          'WHERE    idBlacklisting = %s\n'
                          '     AND tsExpire       = \'infinity\'::TIMESTAMP\n'
                          , (idBlacklisting,));
        self._oDb.maybeCommit(fCommit);
        return True;


    #
    # Helpers.
    #

    def _historizeEntry(self, idBlacklisting, tsExpire = None):
        """
        Historizes the current entry for the given backlisting.
        """
        if tsExpire is None:
            tsExpire = self._oDb.getCurrentTimestamp();
        self._oDb.execute('UPDATE   BuildBlacklist\n'
                          'SET      tsExpire       = %s\n'
                          'WHERE    idBlacklisting = %s\n'
                          '     AND tsExpire       = \'infinity\'::TIMESTAMP\n'
                          , ( tsExpire, idBlacklisting, ));
        return True;

    def _readdEntry(self, uidAuthor, oData, tsEffective = None):
        """
        Re-adds the BuildBlacklist entry. Used by editEntry and removeEntry.
        """
        if tsEffective is None:
            tsEffective = self._oDb.getCurrentTimestamp();
        self._oDb.execute('INSERT INTO BuildBlacklist (\n'
                          '         uidAuthor,\n'
                          '         tsEffective,\n'
                          '         idBlacklisting,\n'
                          '         idFailureReason,\n'
                          '         sProduct,\n'
                          '         sBranch,\n'
                          '         asTypes,\n'
                          '         asOsArches,\n'
                          '         iFirstRevision,\n'
                          '         iLastRevision)\n'
                          'VALUES (%s, %s, %s, %s, %s, %s, %s, %s, %s, %s)\n'
                          , ( uidAuthor,
                              tsEffective,
                              oData.idBlacklisting,
                              oData.idFailureReason,
                              oData.sProduct,
                              oData.sBranch,
                              oData.asTypes,
                              oData.asOsArches,
                              oData.iFirstRevision,
                              oData.iLastRevision,) );
        return True;

