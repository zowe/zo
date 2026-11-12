package org.zowe.zo.ffm;

public interface ZusfService {
    String listUssDir(String path, boolean allFiles, boolean longFormat, int maxDepth) throws Exception;
    String readUssFile(String file, String codepage) throws Exception;
    String writeUssFile(String file, String data, String codepage, String etag) throws Exception;
    void createUssFile(String file, String mode) throws Exception;
    void createUssDir(String file, String mode) throws Exception;
    void moveUssFileOrDir(String source, String destination) throws Exception;
    void chmodUssItem(String file, String mode, boolean recursive) throws Exception;
    void chownUssItem(String file, String owner, boolean recursive) throws Exception;
    void chtagUssItem(String file, String tag, boolean recursive) throws Exception;
    void deleteUssItem(String file, boolean recursive) throws Exception;
}
