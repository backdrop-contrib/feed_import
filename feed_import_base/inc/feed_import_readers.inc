<?php

/**
 * @file
 * This file contains implementations of feed import readers.
 */


/**
 * SimpleXML Reader class, good for small-medium XML files.
 */
class SimpleXMLFIReader extends FeedImportSimpleXPathReader {

  // Namespace uris.
  protected $nsuri;
  // Namespace names.
  protected $nsname;
  // Namespace functions.
  protected $nsfunc;

  /**
   * {@inheritdoc}
   */
  public function init() {
    // Set default required options.
    $this->options += array('class' => 'SimpleXMLElement', 'options' => LIBXML_NOCDATA, 'raw' => '');

    // Check if raw value is submitted.
    if ($this->options['raw'] = trim($this->options['raw'])) {
      try {
        $this->items = simplexml_load_string($this->options['raw'], $this->options['class'], $this->options['options']);
        // Not needed anymore.
        unset($this->options['raw']);
      }
      catch (Exception $e) {
        return FALSE;
      }
    }
    // Try to fetch from URL.
    elseif ($this->options['url']) {
      try {
        $this->items = simplexml_load_file($this->options['url'], $this->options['class'], $this->options['options']);
      }
      catch (Exception $e) {
        return FALSE;
      }
    }
    else {
      // No raw or url resource provided.
      return FALSE;
    }

    // Check for namespace settings.
    if (!empty($this->options['namespaces'])) {
      $this->nsname = $this->nsuri = array();
      foreach ($this->options['namespaces'] as $key => &$ns) {
        $ns = explode('=', $ns, 2);
        if (count($ns) != 2 || empty($ns[0]) || empty($ns[1])) {
          unset($this->options['namespaces'][$key]);
          continue;
        }
        list($this->nsname[], $this->nsuri[]) = $ns;
        // Set namespace.
        $this->items->registerXPathNamespace($ns[0], $ns[1]);
      }

      // Not needed anymore.
      unset($this->options['namespaces'], $ns);
      if ($this->nsname) {
        $this->nsfunc = array(NULL, 'registerXPathNamespace');
      }
    }
    else {
      $this->nsname = $this->nsuri = NULL;
    }

    if (!($this->items = $this->items->xpath($this->options['parent']))) {
      return FALSE;
    }

    return TRUE;
  }

  /**
   * {@inheritdoc}
   */
  public function get() {
    // Get next item.
    $item = array_shift($this->items);
    // Register namespaces if needed.
    if ($this->nsfunc) {
      $this->nsfunc[0] = &$item;
      array_map($this->nsfunc, $this->nsname, $this->nsuri);
    }
    return $item;
  }

}


/**
 * CunkedXML Reader class, used for huge XML files.
 */
class ChunkedXMLFIReader extends FeedImportSimpleXPathReader {
  // Default xml properties.
  private $properties = '<?xml version="1.0" encoding="utf-8"?>';
  // Substring function.
  private $substr = 'substr';
  // Chunk size.
  private $size = 8192;
  // XML parent.
  private $root;

  // Tags info.
  private $tagOpen;
  private $tagClose;
  private $tagLen;
  private $tagCloseLen;

  // Read content.
  private $content = '';
  // Fiel handle.
  private $fh;

  /**
   * {@inheritdoc}
   */
  public function init() {
    // Check for resource location and parent xpath.
    if (empty($this->options['url']) || empty($this->options['parent'])) {
      return FALSE;
    }

    // Check for stream options.
    if ($this->options['stream'] && (
          is_array($this->options['stream']) ||
          $this->options['stream'] = @json_decode($this->options['stream'], TRUE)
        )) {
      try {
        // Open the file using stream options.
        $ctx = stream_context_create($this->options['stream']);
        $this->fh = fopen($this->options['url'], 'rb', FALSE, $ctx);
      }
      catch (Exception $e) {
        return FALSE;
      }
    }
    else {
      try {
        // Open the file.
        $this->fh = fopen($this->options['url'], 'rb');
      }
      catch (Exception $e) {
        return FALSE;
      }
    }

    // Get tag info.
    $this->root = $this->options['parent'];
    $tag = explode('/', $this->options['parent']);
    unset($this->options['parent']);
    $tag = trim(end($tag));

    $this->tagOpen = "<$tag";
    $this->tagClose = "</$tag>";
    $this->tagLen = strlen($tag);
    $this->tagCloseLen = strlen($this->tagClose);

    // Set substr function.
    if (!empty($this->options['substr']) && function_exists($this->options['substr'])) {
      $this->substr = $this->options['substr'];
    }

    // Set XML properties.
    if (!empty($this->options['properties'])) {
      $this->properties = $this->options['properties'];
    }

    // Set chunk size.
    if (!empty($this->options['size'])) {
      $this->size = (int) $this->options['size'];
    }

    // Create an empty array of read items.
    $this->items = array();

    return TRUE;
  }

  /**
   * {@inheritdoc}
   */
  public function get() {
    return ($this->items || $this->read()) ? array_shift($this->items) : NULL;
  }

  /**
   * Populates the $items array with xml nodes.
   */
  protected function read() {
    $substr = &$this->substr;
    // To the end of file.
    while (!feof($this->fh)) {
      // Append read content.
      if (!($this->content .= fread($this->fh, $this->size))) {
        continue;
      };
      // Loop until match some elements.
      while (TRUE) {
        // Tag open position.
        $openpos = strpos($this->content, $this->tagOpen);
        // Tag close position.
        $openposclose = $openpos + $this->tagLen + 1;
        if ($openpos === FALSE ||
            // Tag isn't finished
            !isset($this->content[$openposclose]) ||
            // Check for close tag
            !($closepos = strpos($this->content, $this->tagClose, $openposclose))) {
          if ($this->items) {
            // We have some items.
            break;
          }
          else {
            // Read more data.
            continue 2;
          }
        }
        elseif (isset($this->content[$openposclose]) &&
                $this->content[$openposclose] != ' ' &&
                $this->content[$openposclose] != '>') {
          // Remove data read so far to save memory.
          $this->content = $substr($this->content, $openposclose);
          // Not searched tag, keep looking.
          continue;
        }

        // We have data.
        $closepos += $this->tagCloseLen;

        // Create item.
        $item = $this->properties . $substr($this->content, $openpos, $closepos - $openpos);

        // Remove read data.
        $this->content = $substr($this->content, $closepos);

        // Create xml object.
        try {
          $item = simplexml_load_string($item, 'SimpleXMLElement', LIBXML_NOCDATA);
        }
        catch (Exception $e) {
          continue;
        }

        // Apply root.
        if ($item = $item->xpath($this->root)) {
          // Add to items.
          $this->items[] = reset($item);
        }
        unset($item);
      }
      // We have part of items so just return OK.
      if ($this->items) {
        return TRUE;
      }
    }
    $this->content = '';
    return FALSE;
  }

  /**
   * {@inheritdoc}
   */
  public function __destruct() {
    // Close file handle if any.
    if ($this->fh) {
      try {
        fclose($this->fh);
      }
      catch (Exception $e) {
      }
    }
  }

  /**
   * {@inheritdoc}
   */
  public static function validateOption($option_name, $value) {
    switch ($option_name) {
      case 'stream':
        // Stream setting must be a valid json.
        return !$value || @json_decode($value);
      case 'size':
        return $value > 0;
      case 'properties':
        return (bool) preg_match("/^\<\?xml (.*)\?\>$/", $value);
      case 'substr':
        return in_array($value, array('substr', 'mb_substr', 'drupal_substr'));
    }
    return TRUE;
  }

}



/**
 * HTML Reader class, used for html pages.
 */
class HTMLFIReader extends FeedImportSimpleXPathReader {
  /**
   * {@inheritdoc}
   */
  public function init() {
    // Require url resource and parent xpath.
    if (empty($this->options['url']) || empty($this->options['parent'])) {
      return FALSE;
    }
    // Set default options.
    $this->options += array(
      'report_html_errors' => TRUE,
    );
    // Create DOM Document.
    $dom = new DOMDocument();
    // No error check.
    $dom->strictErrorChecking = FALSE;
    // No whitespace.
    $dom->preserveWhiteSpace = FALSE;
    // Recover enabled.
    $dom->recover = TRUE;
    // Load HTML file from url.
    try {
      if ($this->options['report_html_errors']) {
        $dom->loadHTMLFile($this->options['url']);
      }
      else {
        @$dom->loadHTMLFile($this->options['url']);
      }
    }
    catch (Exception $e) {
      // This try-catch is just to parse the HTML file. Nothing to handle.
    }
    // Normalize document.
    $dom->normalizeDocument();
    // Try to convert to xml.
    try {
      $xml = simplexml_import_dom($dom);
    }
    catch (Exception $e) {
      return FALSE;
    }
    // Not needed anymore.
    unset($dom);

    // Check instance.
    if (!($xml instanceof SimpleXMLElement)) {
      return FALSE;
    }

    // Get items from root.
    $this->items = $this->xpath($this->options['parent']);

    return TRUE;
  }

  /**
   * {@inheritdoc}
   */
  public function get() {
    return array_shift($this->items);
  }

}


/**
 * SQL Reader class, used te read data with SQL queries.
 */
class SQLFIReader extends FeedImportUniVectorReader {

  /**
   * {@inheritdoc}
   */
  public function init() {
    // Require dsn and query.
    if (empty($this->options['dsn']) || empty($this->options['query'])) {
      return FALSE;
    }
    // Set default options.
    $this->options += array(
      'user' => 'root',
      'pass' => NULL,
      'params' => array(),
    );

    // Parse params if needed.
    if (!is_array($this->options['params'])) {
      $params = explode(PHP_EOL, $this->options['params']);
      $this->options['params'] = array();
      foreach ($params as &$param) {
        $param = explode('=', $param, 2);
        if (count($param) == 2) {
          $this->options['params'][$param[0]] = $param[1];
        }
        else {
          $this->options['params'][] = $param[0];
        }
      }
      unset($param, $params);
    }

    try {
      // Connect to db.
      $db = new PDO(
        $this->options['dsn'],
        $this->options['user'],
        $this->options['pass']
      );

      // Prepare query.
      $this->items = $db->prepare(
        $this->options['query'],
        array(PDO::ATTR_CURSOR => PDO::CURSOR_FWDONLY)
      );

      // Execute query
      if (!$this->items->execute($this->options['params'])) {
        // Report that query failed.
        throw new Exception(implode(';', $this->items->errorInfo()), $this->items->errorCode());
      }
    }
    catch (Exception $e) {
      return FALSE;
    }

    return TRUE;
  }

  /**
   * {@inheritdoc}
   */
  public function get() {
    return $this->items->fetch(PDO::FETCH_ASSOC, PDO::FETCH_ORI_NEXT);
  }

}

/**
 * CSV Reader class, used to read csv files.
 */
class CSVFIReader extends FeedImportUniVectorReader {

  // File handle.
  protected $fh;

  // Column names.
  protected $columns = FALSE;

  /**
   * {@inheritdoc}
   */
  public function init() {
    // Require url.
    if (empty($this->options['url'])) {
      return FALSE;
    }

    // Set default options.
    $this->options += array(
      'length' => 0,
      'delimiter' => ',',
      'enclosure' => '"',
      'escape' => '\\',
      'use_column_names' => FALSE,
      'stream' => FALSE,
    );

    // Check for stream options.
    if ($this->options['stream'] && (
          is_array($this->options['stream']) ||
          $this->options['stream'] = @json_decode($this->options['stream'], TRUE)
        )) {
      try {
        // Open the file using stream options.
        $ctx = stream_context_create($this->options['stream']);
        $this->fh = fopen($this->options['url'], 'rb', FALSE, $ctx);
      }
      catch (Exception $e) {
        return FALSE;
      }
    }
    else {
      try {
        // Open the file.
        $this->fh = fopen($this->options['url'], 'rb');
      }
      catch (Exception $e) {
        return FALSE;
      }
    }

    // Check to see if column names are used.
    if ($this->options['use_column_names']) {
      if ($this->columns = $this->get()) {
        $this->columns = array_flip(array_map('trim', $this->columns));
      }
      else {
        return FALSE;
      }
    }

    return TRUE;
  }

  /**
   * {@inheritdoc}
   */
  public function get() {
    return fgetcsv(
      $this->fh,
      $this->options['length'],
      $this->options['delimiter'],
      $this->options['enclosure'],
      $this->options['escape']
    );
  }

  /**
   * {@inheritdoc}
   */
  public function formatPath($path) {
    if ($this->columns && isset($this->columns[$path])) {
      return $this->columns[$path];
    }
    return (int) $path;
  }

  /**
   * {@inheritdoc}
   */
  public function __destruct() {
    // Close file handle if any.
    if ($this->fh) {
      try {
        fclose($this->fh);
      }
      catch (Exception $e) {
      }
    }
  }

  /**
   * {@inheritdoc}
   */
  public static function validateOption($option_name, $value) {
    switch ($option_name) {
      case 'stream':
        // Stream setting must be a valid json.
        return !$value || @json_decode($value);
      case 'length':
        return $value >= 0;
      case 'delimiter': case 'enclosure': case 'escape':
        return strlen($value) == 1;
    }
    return TRUE;
  }
}

/**
 * JSON Reader class, used to read data from json files.
 */
class JSONFIReader extends FeedImportVectorReader {

  /**
   * {@inheritdoc}
   */
  public function init() {
    // Check for url resource.
    if (empty($this->options['url'])) {
      return FALSE;
    }

    // Set default params
    $this->options += array(
      'stream' => FALSE,
      'parent' => FALSE,
    );

     // Check for stream options.
    if ($this->options['stream'] && (
          is_array($this->options['stream']) ||
          $this->options['stream'] = @json_decode($this->options['stream'], TRUE)
        )) {
      try {
        // Open the file using stream options.
        $ctx = stream_context_create($this->options['stream']);
        $this->items = json_decode(file_get_contents($this->options['url'], FALSE, $ctx));
      }
      catch (Exception $e) {
        return FALSE;
      }
    }
    else {
      try {
        // Open the file.
        $this->items = json_decode(file_get_contents($this->options['url']));
      }
      catch (Exception $e) {
        return FALSE;
      }
    }

    // Check for parent
    if ($this->options['parent']) {
      $this->options['parent'] = $this->formatPath($this->options['parent']);
      $this->items = $this->map($this->items, $this->options['parent']);
    }

    return TRUE;
  }

  /**
   * {@inheritdoc}
   */
  public function get() {
    return array_shift($this->items);
  }

  /**
   * {@inheritdoc}
   */
  public static function validateOption($option_name, $value) {
    if ($option_name == 'stream') {
      // Stream setting must be a valid json.
      return !$value || @json_decode($value);
    }
    return TRUE;
  }
}